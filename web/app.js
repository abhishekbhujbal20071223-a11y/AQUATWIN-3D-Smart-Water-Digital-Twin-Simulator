/**
 * ============================================================================
 * AQUATWIN: 3D Smart Water Monitoring and Control Simulator
 * Module: app.js (High-Performance Three.js 3D Twin & Smooth Telemetry Client)
 * ============================================================================
 * Features:
 *  - Fixed & frame-independent delta time (THREE.Clock)
 *  - Smooth visual 60 FPS water surface interpolation
 *  - Non-overlapping self-scheduling telemetry polling
 *  - Immediate slider feedback with throttled network dispatch
 *  - Zero layout-thrashing cached Canvas telemetry chart
 *  - Selective DOM dirty-checking for microsecond UI response
 * ============================================================================
 */

(function () {
    'use strict';

    /* -------------------------------------------------------------------------
     * Constants & Configuration
     * ------------------------------------------------------------------------- */
    const TANK_HEIGHT = 4.0;
    const TANK_RADIUS = 1.2;

    let scene, camera, renderer, controls;
    let threeClock = null;
    let waterMesh, waterSurfaceMesh, pumpImpeller, inflowWaterStream;
    let sensorPulseRing, inflowValveWheel, drainValveWheel;
    let currentWaterColorHex = 0x00d4ff;

    /* Authoritative C Engine state reference */
    let lastSimState = null;
    let visualLevelPct = 45.0; /* Smooth 60 FPS visual interpolation */
    let isConnected = false;
    let isFetching = false;
    let pollTimerId = null;

    /* Slider dragging state flags to prevent jitter */
    let isDraggingInflow = false;
    let isDraggingDrain = false;
    let inflowThrottleTimer = null;
    let drainThrottleTimer = null;

    /* Telemetry Chart Buffer (60 data points) */
    const chartHistory = {
        time: [],
        actual: [],
        raw: [],
        filtered: []
    };
    const MAX_CHART_POINTS = 60;
    let chartCanvasWidth = 0;
    let chartCanvasHeight = 0;

    /* -------------------------------------------------------------------------
     * UI Element References
     * ------------------------------------------------------------------------- */
    const UI = {
        simClock: document.getElementById('simClockDisplay'),
        engineIndicator: document.getElementById('engineStatusIndicator'),
        engineStatusText: document.getElementById('engineStatusText'),
        hudLevel: document.getElementById('hudWaterLevel'),
        hudVolume: document.getElementById('hudWaterVolume'),
        hudNetFlow: document.getElementById('hudNetFlow'),
        alertBanner: document.getElementById('systemAlertBanner'),
        alertStatus: document.getElementById('alertStatusLabel'),
        alertMessage: document.getElementById('alertMessageText'),
        controllerReason: document.getElementById('controllerReasonText'),
        cardModeBadge: document.getElementById('cardModeBadge'),
        metricLevel: document.getElementById('metricWaterLevel'),
        metricVolume: document.getElementById('metricActualVolume'),
        progressBar: document.getElementById('levelProgressBar'),
        btnModeAuto: document.getElementById('btnModeAuto'),
        btnModeManual: document.getElementById('btnModeManual'),
        btnTogglePump: document.getElementById('btnTogglePump'),
        pumpLed: document.getElementById('pumpLed'),
        pumpFlowText: document.getElementById('pumpFlowText'),
        sliderInflow: document.getElementById('sliderInflowValve'),
        inflowValveVal: document.getElementById('inflowValveVal'),
        sliderDrain: document.getElementById('sliderDrainValve'),
        drainValveVal: document.getElementById('drainValveVal'),
        drainFlowText: document.getElementById('drainFlowText'),
        btnPause: document.getElementById('btnPauseSim'),
        btnReset: document.getElementById('btnResetSim'),
        faultDesc: document.getElementById('faultDescText'),
        entcActual: document.getElementById('entcActualLevel'),
        entcRaw: document.getElementById('entcRawLevel'),
        entcFiltered: document.getElementById('entcFilteredLevel'),
        eventLogBox: document.getElementById('eventLogBox'),
        btnClearLog: document.getElementById('btnClearLog'),
        btnResetCam: document.getElementById('btnResetCamera'),
        chartCanvas: document.getElementById('chartCanvas'),
        scenarioBtns: document.querySelectorAll('.btn-scenario')
    };

    /* Helper: Selective DOM text update to avoid layout thrashing */
    function setText(elem, text) {
        if (elem && elem.textContent !== text) {
            elem.textContent = text;
        }
    }

    /* -------------------------------------------------------------------------
     * 1. Three.js 3D Scene Initialization
     * ------------------------------------------------------------------------- */
    function init3D() {
        const container = document.getElementById('viewport3DContainer');
        const width = container.clientWidth || 800;
        const height = container.clientHeight || 600;

        threeClock = new THREE.Clock();

        /* Scene */
        scene = new THREE.Scene();
        scene.background = new THREE.Color(0x080e1e);
        scene.fog = new THREE.FogExp2(0x080e1e, 0.04);

        /* Camera */
        camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 100);
        camera.position.set(5.5, 3.8, 6.2);

        /* Renderer */
        renderer = new THREE.WebGLRenderer({
            canvas: document.getElementById('canvas3d'),
            antialias: true,
            powerPreference: 'high-performance'
        });
        renderer.setSize(width, height);
        renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
        renderer.shadowMap.enabled = true;
        renderer.shadowMap.type = THREE.PCFSoftShadowMap;

        /* OrbitControls on canvas */
        controls = new THREE.OrbitControls(camera, renderer.domElement);
        controls.enableDamping = true;
        controls.dampingFactor = 0.08;
        controls.target.set(0, TANK_HEIGHT * 0.45, 0);
        controls.maxPolarAngle = Math.PI / 2 + 0.05;
        controls.minDistance = 2.8;
        controls.maxDistance = 14.0;

        /* Lighting */
        setupLighting();

        /* Build Components */
        buildEnvironment();
        buildWaterTank();
        buildPipingSystem();
        buildCentrifugalPump();
        buildLevelSensor();

        /* Window & Canvas Resize */
        window.addEventListener('resize', onWindowResize, { passive: true });
        resizeChartCanvas();

        /* Start Animation Loop */
        requestAnimationFrame(animate);
    }

    function setupLighting() {
        const ambientLight = new THREE.AmbientLight(0x1e293b, 1.4);
        scene.add(ambientLight);

        const keyLight = new THREE.DirectionalLight(0xe0f2fe, 1.8);
        keyLight.position.set(6, 10, 5);
        keyLight.castShadow = true;
        keyLight.shadow.mapSize.width = 1024;
        keyLight.shadow.mapSize.height = 1024;
        scene.add(keyLight);

        const fillLight = new THREE.DirectionalLight(0x0284c7, 0.9);
        fillLight.position.set(-6, 4, -4);
        scene.add(fillLight);

        const tankGlow = new THREE.PointLight(0x00f2fe, 1.2, 6);
        tankGlow.position.set(0, 1.6, 0);
        scene.add(tankGlow);
    }

    function buildEnvironment() {
        const gridHelper = new THREE.GridHelper(16, 32, 0x00f2fe, 0x1e293b);
        gridHelper.position.y = 0;
        scene.add(gridHelper);

        const padGeo = new THREE.BoxGeometry(4.0, 0.15, 3.6);
        const padMat = new THREE.MeshStandardMaterial({
            color: 0x131d33,
            roughness: 0.8,
            metalness: 0.2
        });
        const pad = new THREE.Mesh(padGeo, padMat);
        pad.position.set(0, 0.075, 0);
        pad.receiveShadow = true;
        scene.add(pad);
    }

    function buildWaterTank() {
        /* Outer Glass Cylinder */
        const glassGeo = new THREE.CylinderGeometry(TANK_RADIUS, TANK_RADIUS, TANK_HEIGHT, 48, 1, true);
        const glassMat = new THREE.MeshPhysicalMaterial({
            color: 0x7dd3fc,
            transparent: true,
            opacity: 0.25,
            roughness: 0.1,
            metalness: 0.1,
            transmission: 0.85,
            ior: 1.4,
            side: THREE.DoubleSide,
            depthWrite: false
        });
        const glassMesh = new THREE.Mesh(glassGeo, glassMat);
        glassMesh.position.y = TANK_HEIGHT / 2 + 0.15;
        scene.add(glassMesh);

        /* Structural Metal Rings */
        const ringGeo = new THREE.TorusGeometry(TANK_RADIUS + 0.02, 0.04, 16, 48);
        const ringMat = new THREE.MeshStandardMaterial({
            color: 0x64748b,
            metalness: 0.85,
            roughness: 0.3
        });

        [0.15, TANK_HEIGHT * 0.5 + 0.15, TANK_HEIGHT + 0.15].forEach(y => {
            const ring = new THREE.Mesh(ringGeo, ringMat);
            ring.rotation.x = Math.PI / 2;
            ring.position.y = y;
            scene.add(ring);
        });

        /* Tank Base & Top Cap */
        const capGeo = new THREE.CylinderGeometry(TANK_RADIUS + 0.04, TANK_RADIUS + 0.04, 0.08, 48);
        const capMat = new THREE.MeshStandardMaterial({ color: 0x334155, metalness: 0.8, roughness: 0.3 });

        const baseCap = new THREE.Mesh(capGeo, capMat);
        baseCap.position.y = 0.15;
        scene.add(baseCap);

        const topCap = new THREE.Mesh(capGeo, capMat);
        topCap.position.y = TANK_HEIGHT + 0.15;
        scene.add(topCap);

        /* Graduation Marks */
        const marksGroup = new THREE.Group();
        const markMat = new THREE.MeshBasicMaterial({ color: 0x38bdf8 });
        for (let i = 0; i <= 10; i++) {
            const markGeo = new THREE.BoxGeometry(0.12, 0.02, 0.02);
            const mark = new THREE.Mesh(markGeo, markMat);
            const y = 0.15 + (TANK_HEIGHT * (i / 10));
            mark.position.set(TANK_RADIUS + 0.02, y, 0);
            marksGroup.add(mark);
        }
        scene.add(marksGroup);

        /* Water Volume Mesh */
        const waterGeo = new THREE.CylinderGeometry(TANK_RADIUS * 0.98, TANK_RADIUS * 0.98, 1.0, 48, 1, false);
        const waterMat = new THREE.MeshStandardMaterial({
            color: 0x00d4ff,
            transparent: true,
            opacity: 0.72,
            roughness: 0.15,
            metalness: 0.1
        });
        waterMesh = new THREE.Mesh(waterGeo, waterMat);
        waterMesh.position.set(0, 0.15 + 0.5, 0);
        waterMesh.scale.set(1, 0.45 * TANK_HEIGHT, 1);
        scene.add(waterMesh);

        /* Top Water Surface Disc */
        const surfaceGeo = new THREE.CircleGeometry(TANK_RADIUS * 0.98, 48);
        const surfaceMat = new THREE.MeshBasicMaterial({
            color: 0x67e8f9,
            transparent: true,
            opacity: 0.85,
            side: THREE.DoubleSide
        });
        waterSurfaceMesh = new THREE.Mesh(surfaceGeo, surfaceMat);
        waterSurfaceMesh.rotation.x = -Math.PI / 2;
        waterSurfaceMesh.position.set(0, 0.15 + 0.45 * TANK_HEIGHT, 0);
        scene.add(waterSurfaceMesh);
    }

    function buildPipingSystem() {
        const pipeMat = new THREE.MeshStandardMaterial({ color: 0x475569, metalness: 0.75, roughness: 0.35 });

        /* 1. Inflow Pipe */
        const inPipeGroup = new THREE.Group();
        const vPipeGeo = new THREE.CylinderGeometry(0.09, 0.09, 3.6, 24);
        const vPipe = new THREE.Mesh(vPipeGeo, pipeMat);
        vPipe.position.set(-2.0, 1.95, 0);
        inPipeGroup.add(vPipe);

        const hPipeGeo = new THREE.CylinderGeometry(0.09, 0.09, 2.0, 24);
        const hPipe = new THREE.Mesh(hPipeGeo, pipeMat);
        hPipe.rotation.z = Math.PI / 2;
        hPipe.position.set(-1.0, 3.75, 0);
        inPipeGroup.add(hPipe);

        const spoutGeo = new THREE.CylinderGeometry(0.08, 0.08, 0.4, 24);
        const spout = new THREE.Mesh(spoutGeo, pipeMat);
        spout.position.set(0, 3.55, 0);
        inPipeGroup.add(spout);

        const wheelGeo = new THREE.TorusGeometry(0.18, 0.025, 8, 24);
        const wheelMat = new THREE.MeshStandardMaterial({ color: 0x0284c7, metalness: 0.7, roughness: 0.3 });
        inflowValveWheel = new THREE.Mesh(wheelGeo, wheelMat);
        inflowValveWheel.rotation.y = Math.PI / 2;
        inflowValveWheel.position.set(-1.0, 3.75, 0.12);
        inPipeGroup.add(inflowValveWheel);

        scene.add(inPipeGroup);

        /* Inflow Falling Stream */
        const streamGeo = new THREE.CylinderGeometry(0.04, 0.06, 3.0, 16);
        const streamMat = new THREE.MeshBasicMaterial({
            color: 0x38bdf8,
            transparent: true,
            opacity: 0.0
        });
        inflowWaterStream = new THREE.Mesh(streamGeo, streamMat);
        inflowWaterStream.position.set(0, 2.0, 0);
        scene.add(inflowWaterStream);

        /* 2. Drain Pipe */
        const outPipeGroup = new THREE.Group();
        const dPipeGeo = new THREE.CylinderGeometry(0.09, 0.09, 1.8, 24);
        const dPipe = new THREE.Mesh(dPipeGeo, pipeMat);
        dPipe.rotation.z = Math.PI / 2;
        dPipe.position.set(1.9, 0.3, 0);
        outPipeGroup.add(dPipe);

        drainValveWheel = new THREE.Mesh(wheelGeo, new THREE.MeshStandardMaterial({ color: 0xf97316, metalness: 0.7, roughness: 0.3 }));
        drainValveWheel.rotation.y = Math.PI / 2;
        drainValveWheel.position.set(1.9, 0.3, 0.12);
        outPipeGroup.add(drainValveWheel);

        scene.add(outPipeGroup);
    }

    function buildCentrifugalPump() {
        const pumpGroup = new THREE.Group();
        pumpGroup.position.set(-2.0, 0.45, 0);

        const casingGeo = new THREE.CylinderGeometry(0.35, 0.35, 0.3, 24);
        const casingMat = new THREE.MeshStandardMaterial({ color: 0x1e293b, metalness: 0.8, roughness: 0.3 });
        const casing = new THREE.Mesh(casingGeo, casingMat);
        casing.rotation.x = Math.PI / 2;
        pumpGroup.add(casing);

        const motorGeo = new THREE.CylinderGeometry(0.24, 0.24, 0.5, 24);
        const motorMat = new THREE.MeshStandardMaterial({ color: 0x3b82f6, metalness: 0.6, roughness: 0.4 });
        const motor = new THREE.Mesh(motorGeo, motorMat);
        motor.position.set(0, 0, -0.4);
        motor.rotation.x = Math.PI / 2;
        pumpGroup.add(motor);

        const impellerGeo = new THREE.BoxGeometry(0.4, 0.06, 0.03);
        const impellerMat = new THREE.MeshBasicMaterial({ color: 0x10b981 });
        pumpImpeller = new THREE.Mesh(impellerGeo, impellerMat);
        pumpImpeller.position.set(0, 0, 0.16);
        pumpGroup.add(pumpImpeller);

        scene.add(pumpGroup);
    }

    function buildLevelSensor() {
        const sensorGroup = new THREE.Group();
        sensorGroup.position.set(0, TANK_HEIGHT + 0.3, 0);

        const headGeo = new THREE.CylinderGeometry(0.12, 0.12, 0.22, 24);
        const headMat = new THREE.MeshStandardMaterial({ color: 0xf59e0b, metalness: 0.5, roughness: 0.4 });
        const head = new THREE.Mesh(headGeo, headMat);
        sensorGroup.add(head);

        const bracketGeo = new THREE.BoxGeometry(0.6, 0.04, 0.08);
        const bracketMat = new THREE.MeshStandardMaterial({ color: 0x475569 });
        const bracket = new THREE.Mesh(bracketGeo, bracketMat);
        bracket.position.y = 0.12;
        sensorGroup.add(bracket);

        const waveGeo = new THREE.RingGeometry(0.05, 0.08, 32);
        const waveMat = new THREE.MeshBasicMaterial({
            color: 0x00f2fe,
            transparent: true,
            opacity: 0.8,
            side: THREE.DoubleSide
        });
        sensorPulseRing = new THREE.Mesh(waveGeo, waveMat);
        sensorPulseRing.rotation.x = Math.PI / 2;
        sensorPulseRing.position.y = -0.12;
        sensorGroup.add(sensorPulseRing);

        scene.add(sensorGroup);
    }

    function onWindowResize() {
        const container = document.getElementById('viewport3DContainer');
        if (!container || !renderer || !camera) return;
        const width = container.clientWidth;
        const height = container.clientHeight;
        camera.aspect = width / height;
        camera.updateProjectionMatrix();
        renderer.setSize(width, height);
        resizeChartCanvas();
    }

    function resizeChartCanvas() {
        if (!UI.chartCanvas) return;
        const rect = UI.chartCanvas.getBoundingClientRect();
        if (rect.width > 0 && rect.height > 0) {
            chartCanvasWidth = Math.floor(rect.width);
            chartCanvasHeight = Math.floor(rect.height);
            UI.chartCanvas.width = chartCanvasWidth;
            UI.chartCanvas.height = chartCanvasHeight;
            drawChart();
        }
    }

    /* -------------------------------------------------------------------------
     * 2. Animation & Smooth 60 FPS Visual Interpolation
     * ------------------------------------------------------------------------- */
    let animClock = 0;

    function animate() {
        requestAnimationFrame(animate);

        /* Fixed / delta time calculation */
        const dt = threeClock ? Math.min(threeClock.getDelta(), 0.1) : 0.016;
        animClock += dt;

        /* Smooth visual level interpolation towards C authoritative state */
        if (lastSimState) {
            const targetPct = lastSimState.actual_level_pct;
            /* Exponential blend for zero-lag silky smooth animation */
            const blend = 1.0 - Math.exp(-12.0 * dt);
            visualLevelPct += (targetPct - visualLevelPct) * blend;
            updateWater3D(visualLevelPct, animClock);
        }

        /* Frame-rate independent pump impeller spin */
        if (pumpImpeller && lastSimState && lastSimState.pump_actual_running) {
            pumpImpeller.rotation.z += 9.0 * dt;
        }

        /* Pulsing ultrasonic wave animation */
        if (sensorPulseRing) {
            const waveScale = 1.0 + (Math.sin(animClock * 3.5) + 1.0) * 1.5;
            sensorPulseRing.scale.set(waveScale, waveScale, 1);
            sensorPulseRing.material.opacity = Math.max(0.1, 1.0 - (waveScale - 1.0) / 3.0);
        }

        controls.update();
        renderer.render(scene, camera);
    }

    function updateWater3D(levelPct, clock) {
        const clampedPct = Math.max(0.001, Math.min(100.0, levelPct));
        const waterHeight = (clampedPct / 100.0) * TANK_HEIGHT;

        /* Transform existing geometry smoothly */
        waterMesh.scale.y = Math.max(0.01, waterHeight);
        waterMesh.position.y = 0.15 + (waterHeight / 2);

        const surfaceY = 0.15 + waterHeight;
        waterSurfaceMesh.position.y = surfaceY;

        /* Subtle ripple */
        const rip = Math.sin(clock * 3.2) * 0.008;
        waterSurfaceMesh.scale.set(1.0 + rip, 1.0 - rip, 1.0);

        /* Color shift based on PDF level states */
        let targetColor = 0x00d4ff;
        if (lastSimState) {
            if (lastSimState.level_state === 0) targetColor = 0xf59e0b;       /* LOW (Amber) */
            else if (lastSimState.level_state === 1) targetColor = 0x00d4ff;  /* MEDIUM (Aqua) */
            else if (lastSimState.level_state === 2) targetColor = 0xf97316;  /* HIGH (Orange) */
            else if (lastSimState.level_state === 3) targetColor = 0xef4444;  /* OVERFLOW (Red) */
        }

        if (currentWaterColorHex !== targetColor) {
            waterMesh.material.color.setHex(targetColor);
            waterSurfaceMesh.material.color.setHex(targetColor);
            currentWaterColorHex = targetColor;
        }

        /* Inflow waterfall stream effect */
        if (inflowWaterStream && lastSimState) {
            if (lastSimState.inflow_rate_lps > 0.1) {
                inflowWaterStream.material.opacity = 0.75;
                const topY = 3.55;
                const streamLength = Math.max(0.1, topY - surfaceY);
                inflowWaterStream.scale.y = streamLength / 3.0;
                inflowWaterStream.position.y = topY - (streamLength / 2);
            } else {
                inflowWaterStream.material.opacity = 0.0;
            }
        }
    }

    /* -------------------------------------------------------------------------
     * 3. Dual-Mode Simulation Engine (Native C Server + Browser Twin Fallback)
     * -------------------------------------------------------------------------
     * Truthful status reporting:
     *  - C ENGINE: ONLINE (When native aquatwin.exe is connected over localhost)
     *  - SIMULATION: BROWSER TWIN (When running on GitHub Pages or standalone)
     * ------------------------------------------------------------------------- */
    const RUNTIME_MODE = {
        PROBING: 0,
        C_SERVER: 1,
        BROWSER_TWIN: 2
    };

    let currentRuntimeMode = RUNTIME_MODE.PROBING;
    let consecutiveFetchFailures = 0;
    let browserTickerId = null;
    let backgroundProbeId = null;

    function getApiEndpoint(endpoint) {
        /* Avoid leading slash to support subdirectories like /aquatwin/ */
        return endpoint.startsWith('/') ? endpoint.slice(1) : endpoint;
    }

    function isLocalhostHost() {
        const host = window.location.hostname;
        return host === 'localhost' || host === '127.0.0.1' || host === '::1';
    }

    /* -------------------------------------------------------------------------
     * In-Browser Simulation Engine (Faithful implementation of src/engine.c)
     * ------------------------------------------------------------------------- */
    const BrowserSimEngine = {
        state: {
            sim_time_sec: 0.0,
            dt_sec: 0.05,
            is_paused: 0,
            actual_volume_liters: 450.0,
            max_capacity_liters: 1000.0,
            actual_level_pct: 45.0,
            raw_sensor_level_pct: 45.0,
            filtered_level_pct: 45.0,
            sensor_noise_amplitude: 0.0,
            inflow_rate_lps: 0.0,
            outflow_rate_lps: 2.0,
            leak_rate_lps: 0.0,
            mode: 0, /* 0 = AUTO, 1 = MANUAL */
            mode_label: 'AUTO',
            pump_commanded: 0,
            pump_actual_running: 0,
            inflow_valve_pct: 100.0,
            drain_valve_pct: 20.0,
            level_state: 1, /* LEVEL_MEDIUM */
            status_label: 'MEDIUM WATER LEVEL',
            alert_message: 'Message: Water level is in normal range.',
            controller_reason: 'System initialized. Monitoring tank level.',
            current_scenario: 0, /* SCENARIO_NORMAL_FILL */
            fault_description: 'Nominal operation. No faults detected.'
        },

        init() {
            const s = this.state;
            s.sim_time_sec = 0.0;
            s.dt_sec = 0.05;
            s.is_paused = 0;
            s.actual_volume_liters = 450.0;
            s.max_capacity_liters = 1000.0;
            s.actual_level_pct = 45.0;
            s.raw_sensor_level_pct = 45.0;
            s.filtered_level_pct = 45.0;
            s.sensor_noise_amplitude = 0.0;
            s.mode = 0;
            s.mode_label = 'AUTO';
            s.pump_commanded = 0;
            s.pump_actual_running = 0;
            s.inflow_valve_pct = 100.0;
            s.drain_valve_pct = 20.0;
            s.current_scenario = 0;
            s.controller_reason = 'System initialized. Monitoring tank level.';
            s.fault_description = 'Nominal operation. No faults detected.';
            this.step(0.0);
        },

        step(dtSec) {
            const s = this.state;
            if (s.is_paused) return this.getState();
            if (dtSec <= 0.0 && s.sim_time_sec > 0.0) return this.getState();

            s.sim_time_sec += dtSec;

            /* 1. Scenario-dependent Actuator & Fault Handling */
            if (s.current_scenario === 3) { /* SCENARIO_PUMP_FAILURE */
                s.pump_actual_running = 0;
                s.inflow_rate_lps = 0.0;
            } else {
                s.pump_actual_running = s.pump_commanded;
                if (s.pump_actual_running) {
                    s.inflow_rate_lps = 15.0 * (s.inflow_valve_pct / 100.0);
                } else {
                    s.inflow_rate_lps = 0.0;
                }
            }

            const consumerFlow = 10.0 * (s.drain_valve_pct / 100.0);
            if (s.current_scenario === 4) { /* SCENARIO_PIPE_LEAK */
                s.leak_rate_lps = 7.5;
            } else {
                s.leak_rate_lps = 0.0;
            }

            if (s.actual_volume_liters <= 0.001) {
                s.outflow_rate_lps = 0.0;
            } else {
                s.outflow_rate_lps = consumerFlow + s.leak_rate_lps;
            }

            /* 2. Physical Water Mass Balance: dV = (Q_in - Q_out) * dt */
            const netFlow = s.inflow_rate_lps - s.outflow_rate_lps;
            const deltaV = netFlow * dtSec;
            s.actual_volume_liters += deltaV;

            if (s.actual_volume_liters < 0.0) s.actual_volume_liters = 0.0;
            else if (s.actual_volume_liters > s.max_capacity_liters) s.actual_volume_liters = s.max_capacity_liters;

            s.actual_level_pct = (s.actual_volume_liters / s.max_capacity_liters) * 100.0;

            /* 3. ENTC Sensor Modeling & Digital Signal Filtering (EMA) */
            let noise = 0.0;
            if (s.current_scenario === 5) { /* SCENARIO_SENSOR_NOISE */
                noise = (Math.random() * 16.0) - 8.0;
            } else {
                noise = (Math.random() * 0.8) - 0.4;
            }

            s.raw_sensor_level_pct = s.actual_level_pct + noise;
            if (s.raw_sensor_level_pct < 0.0) s.raw_sensor_level_pct = 0.0;
            if (s.raw_sensor_level_pct > 105.0) s.raw_sensor_level_pct = 105.0;

            const alpha = 0.20;
            s.filtered_level_pct = (alpha * s.raw_sensor_level_pct) + ((1.0 - alpha) * s.filtered_level_pct);

            /* 4. State Classification (Preserving exact logic & messages from original PDF) */
            const evalLevel = s.filtered_level_pct;
            if (evalLevel < 30.0) {
                s.level_state = 0; /* LEVEL_LOW */
                s.status_label = 'LOW WATER LEVEL';
                s.alert_message = 'Alert: Check water supply and save water.';
            } else if (evalLevel < 80.0) {
                s.level_state = 1; /* LEVEL_MEDIUM */
                s.status_label = 'MEDIUM WATER LEVEL';
                s.alert_message = 'Message: Water level is in normal range.';
            } else if (evalLevel < 95.0) {
                s.level_state = 2; /* LEVEL_HIGH */
                s.status_label = 'HIGH WATER LEVEL';
                s.alert_message = 'Alert: Stop filling to avoid overflow.';
            } else {
                s.level_state = 3; /* LEVEL_OVERFLOW_RISK */
                s.status_label = 'OVERFLOW RISK';
                s.alert_message = 'CRITICAL: Tank capacity near 100%! Emergency cutoff active.';
            }

            /* 5. Smart Controller & Safety Interlocks */
            if (s.actual_level_pct >= 95.0) {
                if (s.pump_commanded) {
                    s.pump_commanded = 0;
                    s.pump_actual_running = 0;
                    s.controller_reason = 'SAFETY INTERLOCK: Forced pump OFF at >= 95.0% overflow threshold.';
                }
            } else if (s.mode === 0) { /* MODE_AUTO */
                if (evalLevel <= 28.0) {
                    if (!s.pump_commanded) {
                        s.pump_commanded = 1;
                        s.controller_reason = 'Auto Controller: Level <= 28.0%. Pump started.';
                    }
                } else if (evalLevel >= 80.0) {
                    if (s.pump_commanded) {
                        s.pump_commanded = 0;
                        s.controller_reason = 'Auto Controller: Level >= 80.0%. Pump stopped.';
                    }
                }
            }

            return this.getState();
        },

        applyControl(params) {
            const s = this.state;

            /* Mode toggle */
            if (params.mode === 'auto') {
                s.mode = 0;
                s.mode_label = 'AUTO';
                s.controller_reason = 'Switched to AUTOMATIC closed-loop control.';
            } else if (params.mode === 'manual') {
                s.mode = 1;
                s.mode_label = 'MANUAL';
                s.controller_reason = 'Switched to MANUAL operator override.';
            }

            /* Pump toggle */
            if (params.pump !== undefined) {
                const pumpOn = parseInt(params.pump, 10) === 1;
                s.pump_commanded = pumpOn ? 1 : 0;
                if (s.current_scenario === 3) {
                    s.pump_actual_running = 0;
                    s.inflow_rate_lps = 0.0;
                } else {
                    s.pump_actual_running = s.pump_commanded;
                    s.inflow_rate_lps = s.pump_actual_running ? (15.0 * (s.inflow_valve_pct / 100.0)) : 0.0;
                }
                s.controller_reason = `Operator manually set pump to ${s.pump_commanded ? 'ON.' : 'OFF.'}`;
            }

            /* Inflow valve */
            if (params.inflow !== undefined) {
                let v = parseFloat(params.inflow);
                if (isNaN(v)) v = 100.0;
                s.inflow_valve_pct = Math.max(0.0, Math.min(100.0, v));
                if (s.pump_actual_running) {
                    s.inflow_rate_lps = 15.0 * (s.inflow_valve_pct / 100.0);
                } else {
                    s.inflow_rate_lps = 0.0;
                }
            }

            /* Drain valve */
            if (params.drain !== undefined) {
                let v = parseFloat(params.drain);
                if (isNaN(v)) v = 20.0;
                s.drain_valve_pct = Math.max(0.0, Math.min(100.0, v));
                if (s.actual_volume_liters > 0.001) {
                    const cFlow = 10.0 * (s.drain_valve_pct / 100.0);
                    s.outflow_rate_lps = cFlow + s.leak_rate_lps;
                }
            }

            /* Predefined scenarios (0 to 5) */
            if (params.scenario !== undefined) {
                const sc = parseInt(params.scenario, 10);
                if (sc >= 0 && sc <= 5) {
                    s.current_scenario = sc;
                    switch (sc) {
                        case 0:
                            s.drain_valve_pct = 15.0;
                            s.inflow_valve_pct = 100.0;
                            s.leak_rate_lps = 0.0;
                            s.fault_description = 'Scenario 1: Normal filling cycle (Inflow > Outflow).';
                            break;
                        case 1:
                            s.drain_valve_pct = 85.0;
                            s.inflow_valve_pct = 100.0;
                            s.leak_rate_lps = 0.0;
                            s.fault_description = 'Scenario 2: Heavy consumer demand (High drain rate).';
                            break;
                        case 2:
                            s.drain_valve_pct = 0.0;
                            s.inflow_valve_pct = 100.0;
                            s.leak_rate_lps = 0.0;
                            s.mode = 1;
                            s.mode_label = 'MANUAL';
                            s.pump_commanded = 1;
                            s.pump_actual_running = 1;
                            s.inflow_rate_lps = 15.0;
                            s.controller_reason = 'Operator manually set pump to ON.';
                            s.fault_description = 'Scenario 3: Overflow hazard injection (Pump forced ON).';
                            break;
                        case 3:
                            s.drain_valve_pct = 30.0;
                            s.leak_rate_lps = 0.0;
                            s.pump_commanded = 1;
                            s.pump_actual_running = 0;
                            s.inflow_rate_lps = 0.0;
                            s.fault_description = 'Scenario 4: Pump electrical/mechanical stall fault.';
                            break;
                        case 4:
                            s.drain_valve_pct = 20.0;
                            s.leak_rate_lps = 7.5;
                            s.fault_description = 'Scenario 5: Pipeline rupture (Unaccounted drain leakage).';
                            break;
                        case 5:
                            s.leak_rate_lps = 0.0;
                            s.fault_description = 'Scenario 6: Transducer electrical EMI noise & filter evaluation.';
                            break;
                    }
                    if (s.actual_volume_liters > 0.001) {
                        const cFlow = 10.0 * (s.drain_valve_pct / 100.0);
                        s.outflow_rate_lps = cFlow + s.leak_rate_lps;
                    }
                }
            }

            /* Pause toggle */
            if (params.pause !== undefined) {
                s.is_paused = parseInt(params.pause, 10) === 1 ? 1 : 0;
            }

            /* Reset */
            if (params.reset !== undefined && parseInt(params.reset, 10) === 1) {
                this.init();
            }

            return this.getState();
        },

        getState() {
            return Object.assign({}, this.state);
        }
    };

    function updateEngineIndicatorUI(mode, subtext) {
        const pulseDot = UI.engineIndicator ? UI.engineIndicator.querySelector('.pulse-dot') : null;
        if (!UI.engineIndicator || !UI.engineStatusText) return;

        if (mode === RUNTIME_MODE.C_SERVER) {
            UI.engineIndicator.style.background = 'rgba(16, 185, 129, 0.12)';
            UI.engineIndicator.style.borderColor = 'rgba(16, 185, 129, 0.3)';
            UI.engineIndicator.style.color = '#10b981';
            setText(UI.engineStatusText, 'C ENGINE: ONLINE');
            if (pulseDot) {
                pulseDot.style.background = '#10b981';
                pulseDot.style.boxShadow = '0 0 8px #10b981';
            }
        } else if (mode === RUNTIME_MODE.BROWSER_TWIN) {
            UI.engineIndicator.style.background = 'rgba(56, 189, 248, 0.12)';
            UI.engineIndicator.style.borderColor = 'rgba(56, 189, 248, 0.35)';
            UI.engineIndicator.style.color = '#38bdf8';
            const label = subtext ? `SIMULATION: BROWSER TWIN (${subtext})` : 'SIMULATION: BROWSER TWIN';
            setText(UI.engineStatusText, label);
            if (pulseDot) {
                pulseDot.style.background = '#38bdf8';
                pulseDot.style.boxShadow = '0 0 8px #38bdf8';
            }
        } else {
            UI.engineIndicator.style.background = 'rgba(239, 68, 68, 0.12)';
            UI.engineIndicator.style.borderColor = 'rgba(239, 68, 68, 0.3)';
            UI.engineIndicator.style.color = '#ef4444';
            setText(UI.engineStatusText, 'C ENGINE: OFFLINE');
            if (pulseDot) {
                pulseDot.style.background = '#ef4444';
                pulseDot.style.boxShadow = '0 0 8px #ef4444';
            }
        }
    }

    function startBrowserSimulation(subtext = 'STANDALONE') {
        currentRuntimeMode = RUNTIME_MODE.BROWSER_TWIN;
        updateEngineIndicatorUI(RUNTIME_MODE.BROWSER_TWIN, subtext);

        if (!lastSimState) {
            BrowserSimEngine.init();
            const initData = BrowserSimEngine.getState();
            lastSimState = initData;
            updateDashboard(initData);
            updateChartHistory(initData);
        }

        if (!browserTickerId) {
            browserTickerId = setInterval(() => {
                if (currentRuntimeMode !== RUNTIME_MODE.BROWSER_TWIN) return;
                const data = BrowserSimEngine.step(0.05);
                lastSimState = data;
                updateDashboard(data);
                updateChartHistory(data);
            }, 50);
        }

        /* If on localhost, gently probe every 6 seconds to see if C server launched */
        if (isLocalhostHost() && !backgroundProbeId) {
            backgroundProbeId = setInterval(async () => {
                if (currentRuntimeMode !== RUNTIME_MODE.BROWSER_TWIN) return;
                try {
                    const c = new AbortController();
                    const t = setTimeout(() => c.abort(), 900);
                    const r = await fetch(getApiEndpoint('api/state'), {
                        cache: 'no-store',
                        headers: { 'Accept': 'application/json' },
                        signal: c.signal
                    });
                    clearTimeout(t);
                    if (r.ok) {
                        const cData = await r.json();
                        switchToCServerMode(cData);
                    }
                } catch (e) {
                    /* Silent keep in browser twin mode */
                }
            }, 6000);
        }
    }

    function switchToCServerMode(data) {
        currentRuntimeMode = RUNTIME_MODE.C_SERVER;
        consecutiveFetchFailures = 0;
        if (browserTickerId) {
            clearInterval(browserTickerId);
            browserTickerId = null;
        }
        if (backgroundProbeId) {
            clearInterval(backgroundProbeId);
            backgroundProbeId = null;
        }

        updateEngineIndicatorUI(RUNTIME_MODE.C_SERVER);
        lastSimState = data;
        updateDashboard(data);
        updateChartHistory(data);

        startPollingLoop();
    }

    async function fetchState() {
        if (currentRuntimeMode === RUNTIME_MODE.BROWSER_TWIN) return;

        try {
            const controller = new AbortController();
            const timeoutId = setTimeout(() => controller.abort(), 1200);

            const resp = await fetch(getApiEndpoint('api/state'), {
                cache: 'no-store',
                headers: { 'Accept': 'application/json' },
                signal: controller.signal
            });
            clearTimeout(timeoutId);

            if (!resp.ok) throw new Error('HTTP ' + resp.status);
            const data = await resp.json();

            consecutiveFetchFailures = 0;
            if (currentRuntimeMode !== RUNTIME_MODE.C_SERVER) {
                currentRuntimeMode = RUNTIME_MODE.C_SERVER;
                updateEngineIndicatorUI(RUNTIME_MODE.C_SERVER);
            }
            lastSimState = data;
            updateDashboard(data);
            updateChartHistory(data);
        } catch (err) {
            consecutiveFetchFailures++;
            if (currentRuntimeMode === RUNTIME_MODE.C_SERVER) {
                if (consecutiveFetchFailures >= 2) {
                    startBrowserSimulation('C SERVER OFFLINE');
                }
            } else {
                const subtext = window.location.hostname.endsWith('github.io') ? 'GITHUB PAGES' : 'STANDALONE';
                startBrowserSimulation(subtext);
            }
        }
    }

    /* Self-scheduling polling loop (avoids overlapping calls) */
    async function startPollingLoop() {
        if (currentRuntimeMode === RUNTIME_MODE.BROWSER_TWIN) {
            return;
        }

        if (!isFetching) {
            isFetching = true;
            await fetchState();
            isFetching = false;
        }

        if (currentRuntimeMode === RUNTIME_MODE.C_SERVER || currentRuntimeMode === RUNTIME_MODE.PROBING) {
            pollTimerId = setTimeout(startPollingLoop, 100);
        }
    }

    async function sendControl(params) {
        if (currentRuntimeMode === RUNTIME_MODE.BROWSER_TWIN) {
            const updated = BrowserSimEngine.applyControl(params);
            lastSimState = updated;
            updateDashboard(updated);
            return;
        }

        try {
            const queryStr = Object.entries(params)
                .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
                .join('&');

            const resp = await fetch(getApiEndpoint('api/control?') + queryStr, {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: queryStr
            });
            if (resp.ok) {
                const updated = await resp.json();
                lastSimState = updated;
                updateDashboard(updated);
            }
        } catch (err) {
            const fallbackState = BrowserSimEngine.applyControl(params);
            lastSimState = fallbackState;
            updateDashboard(fallbackState);
        }
    }

    /* -------------------------------------------------------------------------
     * 4. Selective DOM Dashboard Updates
     * ------------------------------------------------------------------------- */
    let lastLoggedState = -1;
    let lastLoggedPump = -1;

    function updateDashboard(s) {
        setText(UI.simClock, `T = ${s.sim_time_sec.toFixed(1)}s`);

        /* HUD & Metric Displays */
        setText(UI.hudLevel, `${s.actual_level_pct.toFixed(1)}%`);
        setText(UI.hudVolume, `${s.actual_volume_liters.toFixed(1)} L`);
        const netFlow = s.inflow_rate_lps - s.outflow_rate_lps;
        setText(UI.hudNetFlow, `${netFlow >= 0 ? '+' : ''}${netFlow.toFixed(1)} L/s`);

        setText(UI.metricLevel, `${s.actual_level_pct.toFixed(1)}%`);
        setText(UI.metricVolume, `${s.actual_volume_liters.toFixed(1)} L`);
        UI.progressBar.style.width = `${Math.min(100, Math.max(0, s.actual_level_pct))}%`;

        /* Status & Baseline PDF Alerts */
        setText(UI.alertStatus, s.status_label);
        setText(UI.alertMessage, s.alert_message);
        setText(UI.controllerReason, s.controller_reason);
        setText(UI.cardModeBadge, s.mode_label);

        /* Alert Banner Styling */
        const expectedBannerClass = s.level_state === 0 ? 'alert-banner state-low' :
                                    s.level_state === 1 ? 'alert-banner state-medium' :
                                    s.level_state === 2 ? 'alert-banner state-high' :
                                    'alert-banner state-overflow';
        if (UI.alertBanner.className !== expectedBannerClass) {
            UI.alertBanner.className = expectedBannerClass;
        }

        /* Actuator Status */
        const isPumpRunning = s.pump_actual_running;
        const expectedLedClass = isPumpRunning ? 'indicator-led active' :
                                (s.pump_commanded && !isPumpRunning) ? 'indicator-led fault' :
                                'indicator-led';
        if (UI.pumpLed.className !== expectedLedClass) {
            UI.pumpLed.className = expectedLedClass;
        }
        setText(UI.pumpFlowText, `${s.inflow_rate_lps.toFixed(1)} L/s`);

        /* Pump Manual / Auto Button */
        if (s.mode === 1) { /* MANUAL */
            UI.btnTogglePump.disabled = false;
            setText(UI.btnTogglePump, s.pump_commanded ? 'Pump OFF' : 'Pump ON');
            UI.btnTogglePump.className = s.pump_commanded ? 'btn-action btn-danger' : 'btn-action';
        } else {
            UI.btnTogglePump.disabled = true;
            setText(UI.btnTogglePump, s.pump_commanded ? 'AUTO: RUNNING' : 'AUTO: IDLE');
            UI.btnTogglePump.className = 'btn-action';
        }

        /* Sync sliders only when user is NOT dragging them */
        if (!isDraggingInflow) {
            setText(UI.inflowValveVal, `${Math.round(s.inflow_valve_pct)}%`);
            if (document.activeElement !== UI.sliderInflow) {
                UI.sliderInflow.value = Math.round(s.inflow_valve_pct);
            }
        }
        if (!isDraggingDrain) {
            setText(UI.drainValveVal, `${Math.round(s.drain_valve_pct)}%`);
            if (document.activeElement !== UI.sliderDrain) {
                UI.sliderDrain.value = Math.round(s.drain_valve_pct);
            }
        }
        setText(UI.drainFlowText, `${s.outflow_rate_lps.toFixed(1)} L/s`);

        /* Mode Buttons */
        if (s.mode === 0) {
            UI.btnModeAuto.classList.add('active');
            UI.btnModeManual.classList.remove('active');
        } else {
            UI.btnModeAuto.classList.remove('active');
            UI.btnModeManual.classList.add('active');
        }

        setText(UI.btnPause, s.is_paused ? 'Resume' : 'Pause');
        setText(UI.faultDesc, s.fault_description);

        /* ENTC Signal Filter Telemetry */
        setText(UI.entcActual, `${s.actual_level_pct.toFixed(2)}%`);
        setText(UI.entcRaw, `${s.raw_sensor_level_pct.toFixed(2)}%`);
        setText(UI.entcFiltered, `${s.filtered_level_pct.toFixed(2)}%`);

        /* 3D Valve Wheel Rotations */
        if (inflowValveWheel) {
            inflowValveWheel.rotation.x = (s.inflow_valve_pct / 100.0) * Math.PI * 2;
        }
        if (drainValveWheel) {
            drainValveWheel.rotation.x = (s.drain_valve_pct / 100.0) * Math.PI * 2;
        }

        /* Scenarios Active Badge */
        UI.scenarioBtns.forEach(btn => {
            const sc = parseInt(btn.getAttribute('data-scenario'), 10);
            if (sc === s.current_scenario) {
                btn.classList.add('active');
            } else {
                btn.classList.remove('active');
            }
        });

        /* Event Logging for transitions */
        if (s.level_state !== lastLoggedState && lastLoggedState !== -1) {
            const timeStr = `[${String(Math.floor(s.sim_time_sec / 60)).padStart(2, '0')}:${String((s.sim_time_sec % 60).toFixed(1)).padStart(4, '0')}]`;
            let levelClass = (s.level_state === 0 || s.level_state === 2) ? 'warn' :
                             (s.level_state === 3) ? 'alert' : '';
            addLogEntry(timeStr, `State: ${s.status_label} (${s.alert_message})`, levelClass);
        }
        lastLoggedState = s.level_state;

        if (s.pump_actual_running !== lastLoggedPump && lastLoggedPump !== -1) {
            const timeStr = `[${String(Math.floor(s.sim_time_sec / 60)).padStart(2, '0')}:${String((s.sim_time_sec % 60).toFixed(1)).padStart(4, '0')}]`;
            addLogEntry(timeStr, `Pump ${s.pump_actual_running ? 'STARTED' : 'STOPPED'} - ${s.controller_reason}`);
        }
        lastLoggedPump = s.pump_actual_running;
    }

    function addLogEntry(timeStr, message, cssClass = '') {
        const entry = document.createElement('div');
        entry.className = `log-entry ${cssClass}`.trim();
        entry.innerHTML = `<span class="time">${timeStr}</span><span>${message}</span>`;
        UI.eventLogBox.prepend(entry);
        while (UI.eventLogBox.children.length > 35) {
            UI.eventLogBox.removeChild(UI.eventLogBox.lastChild);
        }
    }

    /* -------------------------------------------------------------------------
     * 5. High-Performance Canvas Chart (Zero Layout Thrashing)
     * ------------------------------------------------------------------------- */
    function updateChartHistory(s) {
        chartHistory.time.push(s.sim_time_sec);
        chartHistory.actual.push(s.actual_level_pct);
        chartHistory.raw.push(s.raw_sensor_level_pct);
        chartHistory.filtered.push(s.filtered_level_pct);

        if (chartHistory.time.length > MAX_CHART_POINTS) {
            chartHistory.time.shift();
            chartHistory.actual.shift();
            chartHistory.raw.shift();
            chartHistory.filtered.shift();
        }

        drawChart();
    }

    function drawChart() {
        const canvas = UI.chartCanvas;
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        const w = chartCanvasWidth || canvas.width;
        const h = chartCanvasHeight || canvas.height;
        if (w <= 0 || h <= 0) return;

        const padX = 35;
        const padY = 15;
        const chartW = w - padX - 10;
        const chartH = h - (padY * 2);

        ctx.clearRect(0, 0, w, h);

        /* Setpoint reference lines (30%, 80%, 95%) */
        const levels = [
            { pct: 30, color: 'rgba(245, 158, 11, 0.4)', dash: [4, 4], label: '30%' },
            { pct: 80, color: 'rgba(249, 115, 22, 0.4)', dash: [4, 4], label: '80%' },
            { pct: 95, color: 'rgba(239, 68, 68, 0.5)', dash: [4, 4], label: '95%' }
        ];

        levels.forEach(lv => {
            const y = padY + chartH - (lv.pct / 100.0) * chartH;
            ctx.strokeStyle = lv.color;
            ctx.setLineDash(lv.dash);
            ctx.beginPath();
            ctx.moveTo(padX, y);
            ctx.lineTo(w - 10, y);
            ctx.stroke();

            ctx.fillStyle = lv.color;
            ctx.font = '9px Consolas, monospace';
            ctx.fillText(lv.label, 8, y + 3);
        });
        ctx.setLineDash([]);

        if (chartHistory.actual.length < 2) return;

        const n = chartHistory.actual.length;
        const dx = chartW / (MAX_CHART_POINTS - 1);

        /* Raw Sensor Noise (Amber) */
        ctx.strokeStyle = '#fbbf24';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (let i = 0; i < n; i++) {
            const x = padX + i * dx;
            const y = padY + chartH - (chartHistory.raw[i] / 100.0) * chartH;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();

        /* Actual Ground Truth Level (Bright Cyan) */
        ctx.strokeStyle = '#38bdf8';
        ctx.lineWidth = 2.2;
        ctx.beginPath();
        for (let i = 0; i < n; i++) {
            const x = padX + i * dx;
            const y = padY + chartH - (chartHistory.actual[i] / 100.0) * chartH;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();
    }

    /* -------------------------------------------------------------------------
     * 6. Smooth Event Listeners & Throttled Slider Controls
     * ------------------------------------------------------------------------- */
    function setupEventListeners() {
        /* Auto / Manual Mode */
        UI.btnModeAuto.addEventListener('click', (e) => {
            e.stopPropagation();
            sendControl({ mode: 'auto' });
        });
        UI.btnModeManual.addEventListener('click', (e) => {
            e.stopPropagation();
            sendControl({ mode: 'manual' });
        });

        /* Pump Manual Toggle */
        UI.btnTogglePump.addEventListener('click', (e) => {
            e.stopPropagation();
            if (!lastSimState || lastSimState.mode !== 1) return;
            const nextState = lastSimState.pump_commanded ? 0 : 1;
            sendControl({ pump: nextState });
        });

        /* Inflow Valve Slider with Zero-Lag Dragging */
        UI.sliderInflow.addEventListener('pointerdown', () => { isDraggingInflow = true; }, { passive: true });
        UI.sliderInflow.addEventListener('input', (e) => {
            isDraggingInflow = true;
            setText(UI.inflowValveVal, `${e.target.value}%`);
            if (inflowThrottleTimer) clearTimeout(inflowThrottleTimer);
            inflowThrottleTimer = setTimeout(() => {
                sendControl({ inflow: e.target.value });
            }, 60);
        }, { passive: true });
        UI.sliderInflow.addEventListener('change', (e) => {
            isDraggingInflow = false;
            if (inflowThrottleTimer) clearTimeout(inflowThrottleTimer);
            sendControl({ inflow: e.target.value });
        });
        UI.sliderInflow.addEventListener('pointerup', () => { isDraggingInflow = false; }, { passive: true });

        /* Drain Valve Slider with Zero-Lag Dragging */
        UI.sliderDrain.addEventListener('pointerdown', () => { isDraggingDrain = true; }, { passive: true });
        UI.sliderDrain.addEventListener('input', (e) => {
            isDraggingDrain = true;
            setText(UI.drainValveVal, `${e.target.value}%`);
            if (drainThrottleTimer) clearTimeout(drainThrottleTimer);
            drainThrottleTimer = setTimeout(() => {
                sendControl({ drain: e.target.value });
            }, 60);
        }, { passive: true });
        UI.sliderDrain.addEventListener('change', (e) => {
            isDraggingDrain = false;
            if (drainThrottleTimer) clearTimeout(drainThrottleTimer);
            sendControl({ drain: e.target.value });
        });
        UI.sliderDrain.addEventListener('pointerup', () => { isDraggingDrain = false; }, { passive: true });

        /* Pause Button */
        UI.btnPause.addEventListener('click', (e) => {
            e.stopPropagation();
            if (!lastSimState) return;
            const nextPause = lastSimState.is_paused ? 0 : 1;
            sendControl({ pause: nextPause });
        });

        /* Reset Button */
        UI.btnReset.addEventListener('click', (e) => {
            e.stopPropagation();
            if (confirm('Reset AquaTwin simulation to baseline state?')) {
                sendControl({ reset: 1 });
                chartHistory.time = [];
                chartHistory.actual = [];
                chartHistory.raw = [];
                chartHistory.filtered = [];
                visualLevelPct = 45.0;
                addLogEntry('[00:00.0]', 'Simulation reset to baseline configuration.');
            }
        });

        /* Scenario Buttons */
        UI.scenarioBtns.forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                const scenId = btn.getAttribute('data-scenario');
                sendControl({ scenario: scenId });
            });
        });

        /* Reset Camera View */
        UI.btnResetCam.addEventListener('click', (e) => {
            e.stopPropagation();
            camera.position.set(5.5, 3.8, 6.2);
            controls.target.set(0, TANK_HEIGHT * 0.45, 0);
            controls.update();
        });

        /* Clear Log */
        UI.btnClearLog.addEventListener('click', (e) => {
            e.stopPropagation();
            UI.eventLogBox.innerHTML = '';
        });
    }

    /* -------------------------------------------------------------------------
     * 7. Bootstrap Lifecycle
     * ------------------------------------------------------------------------- */
    window.addEventListener('DOMContentLoaded', () => {
        init3D();
        setupEventListeners();
        startPollingLoop();
    });

})();
