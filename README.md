# AQUATWIN: 3D Smart Water Monitoring and Control Simulator

[![Language: C](https://img.shields.io/badge/Core%20Engine-C99%2FGNU99-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Communication: Winsock HTTP REST](https://img.shields.io/badge/Telemetry-Native%20Winsock2-success.svg)](https://learn.microsoft.com/en-us/windows/win32/winsock/)
[![Frontend: Three.js 3D](https://img.shields.io/badge/Visualization-Three.js%20r128-cyan.svg)](https://threejs.org/)
[![Platform: Windows Laptop](https://img.shields.io/badge/Platform-Windows%2010%2F11-lightgrey.svg)]()
[![Hardware Requirement: None](https://img.shields.io/badge/Hardware-100%25%20Pure%20Software-brightgreen.svg)]()

AquaTwin is a laptop-only **3D Smart Water Monitoring and Control Digital Twin**. It elevates basic C-language console water-level classification into a dynamic, physically consistent engineering simulation featuring a **C-based mass balance physics engine**, closed-loop **hysteresis automation**, **ENTC digital signal filtering**, and an interactive **Three.js 3D industrial cockpit**.

---

## 1. Preserved Baseline & Architectural Evolution

AquaTwin directly preserves and honors the core alert classification rules from the original baseline C program (*Water Level Monitoring System*):

| Water Level (%) | Baseline Classification | Baseline Preserved Alert / Message | AquaTwin Enhanced Actions |
| :--- | :--- | :--- | :--- |
| **$0\% \le L < 30\%$** | **LOW WATER LEVEL** | `"Alert: Check water supply and save water."` | Amber reservoir glow, auto-start feed pump ($15\text{ L/s}$), log warning event. |
| **$30\% \le L < 80\%$** | **MEDIUM WATER LEVEL** | `"Message: Water level is in normal range."` | Aqua-blue fluid glow, nominal valve modulation, maintain hysteresis deadband. |
| **$80\% \le L < 95\%$** | **HIGH WATER LEVEL** | `"Alert: Stop filling to avoid overflow."` | Warm orange glow, auto-stop feed pump ($0\text{ L/s}$), anti-chatter lockout. |
| **$95\% \le L \le 100\%$** | **OVERFLOW RISK** | `"CRITICAL: Tank capacity near 100%! Emergency cutoff active."` | Flashing red alarm, hard safety interlock trips pump OFF across all modes. |

---

## 2. System Architecture

```
+-----------------------------------------------------------------------------------------+
|                                    AQUATWIN SYSTEM                                      |
+-----------------------------------------------------------------------------------------+
|                                                                                         |
|  [ 1. AUTHORITATIVE C SIMULATION & CONTROL ENGINE ]                                     |
|  ├── Physics Engine: dV = (Q_in - Q_out) * dt (Mass conservation, clamped [0, 1000L])   |
|  ├── Control Module: Dual-threshold Schmitt trigger hysteresis (28% start, 80% stop)    |
|  ├── Safety Interlocks: Hard 95% overfill cutoff & dry-run drain limiter                |
|  ├── ENTC Signal Conditioning: Synthetic sensor noise + Exponential Moving Average (EMA)|
|  └── Scenario Engine: 6 selectable fault injection states                               |
|                                                                                         |
|                                            │                                            |
|                                            ▼                                            |
|  [ 2. ZERO-DEPENDENCY COMMUNICATION LAYER ]                                             |
|  ├── Embedded Win32 Winsock2 HTTP Server (ws2_32)                                       |
|  ├── GET  /api/state   -> Real-time JSON telemetry stream (20 Hz loop)                   |
|  ├── POST /api/control -> Mode toggle, actuator override, scenario injection            |
|  └── Static Web Server -> Delivers HTML, CSS, Three.js without requiring Node or Python |
|                                                                                         |
|                                            │ HTTP / JSON                                |
|                                            ▼                                            |
|  [ 3. 3D DIGITAL TWIN & ENGINEERING DASHBOARD ]                                         |
|  ├── Three.js 3D Tank: Glass reservoir, animated fluid volume, dynamic ripple shaders   |
|  ├── Actuator Visuals: Rotating centrifugal pump impeller, animated water stream        |
|  ├── Telemetry HUD: Real-time gauges, volume meters, state pills, and timecodes          |
|  ├── Live Chart: Dual-trace canvas plotting ground truth vs raw noisy transducer readings|
|  └── Event Log: Chronological audit trail of controller actions and safety trips        |
+-----------------------------------------------------------------------------------------+
```

---

## 3. Core ENTC (Electronics & Telecommunication) Concepts Explained

### A. Discrete Physical Integration (Conservation of Mass)
In fluid mechanics and industrial instrumentation, liquid volume cannot jump instantaneously. Water accumulation is governed by:
$$\frac{dV(t)}{dt} = Q_{\text{in}}(t) - Q_{\text{out}}(t)$$
AquaTwin computes this deterministically at fixed discrete time steps ($\Delta t = 0.05\text{ s}$ / $20\text{ Hz}$):
$$V[k] = \text{clamp}\left(V[k-1] + \left(Q_{\text{in}}[k] - Q_{\text{out}}[k]\right) \cdot \Delta t,\; 0,\; V_{\text{max}}\right)$$
The displayed percentage is strictly calculated from the ground-truth volume:
$$L[k] = \frac{V[k]}{V_{\text{max}}} \times 100\%$$

### B. Anti-Chatter Dual-Threshold Hysteresis Control
If a simple single threshold were used (e.g. Pump ON if $<50\%$, Pump OFF if $\ge 50\%$), surface sloshing and inflow turbulence would cause rapid ON/OFF oscillation (contact chatter), rapidly wearing out relays, contactors, and motor windings.
AquaTwin implements an ENTC Schmitt-Trigger Hysteresis Loop:
* **Lower Setpoint ($L_{\text{start}} = 28.0\%$)**: Pump turns ON.
* **Upper Setpoint ($L_{\text{stop}} = 80.0\%$)**: Pump turns OFF.
* **Deadband ($28\% < L < 80\%$)**: Pump retains its previous state, preventing chatter.

### C. Transducer Noise & Digital Low-Pass Filtering (EMA)
Industrial level transducers (ultrasonic pulse-echo, hydrostatic pressure transmitters) suffer from electromagnetic interference (EMI), ADC quantization error, and surface wave agitation.
In Scenario 6 (*Sensor Noise*), synthetic white noise is superimposed onto the sensor reading:
$$L_{\text{raw}}[k] = L_{\text{actual}}[k] + \mathcal{N}\left(-\sigma, +\sigma\right)$$
An **Exponential Moving Average (EMA)** single-pole digital low-pass filter is implemented:
$$\hat{L}[k] = \alpha \cdot L_{\text{raw}}[k] + (1 - \alpha) \cdot \hat{L}[k-1]$$
With $\alpha = 0.20$, high-frequency jitter is filtered out while maintaining rapid response to real volume changes.

### D. Safety Interlocks & Fail-Safe Design
In industrial Distributed Control Systems (DCS) / SCADA:
1. **High-Level Overfill Interlock**: If water level reaches $\ge 95.0\%$, a hardware-equivalent safety trip forces the pump OFF regardless of whether the operator is in Automatic or Manual override mode.
2. **Dry-Run Interlock**: When tank volume reaches $0.0\text{ L}$, physical outflow is forced to $0\text{ L/s}$ because gravity head pressure collapses.

---

## 4. Project Directory Structure

```
d:/AQUATWIN/
├── legacy/
│   ├── water_monitor_legacy.c     # Original PDF C code preserved exactly
│   └── water_monitor_legacy.exe   # Compiled legacy executable
├── src/
│   ├── engine.h                   # Physics, actuator & telemetry data structures
│   ├── engine.c                   # Simulation math, hysteresis, EMA filter & JSON export
│   ├── server.h                   # Win32 Winsock HTTP server interface
│   ├── server.c                   # HTTP 1.1 REST & static asset dispatcher
│   └── main.c                     # Server entry point & CLI parser
├── tests/
│   ├── test_engine.c              # Automated unit and integration test suite
│   └── test_engine.exe            # Compiled test binary (7/7 tests passed)
├── web/
│   ├── index.html                 # Complete 3D dashboard & telemetry HUD
│   ├── style.css                  # Dark engineering aesthetics & glassmorphism
│   ├── app.js                     # Three.js 3D scene, REST client & Canvas chart
│   ├── three.min.js               # Three.js r128 (bundled for 100% offline use)
│   └── OrbitControls.js           # 360-degree camera controls (offline bundled)
├── aquatwin.exe                   # Complete standalone application binary
└── README.md                      # Complete architectural and academic documentation
```

---

## 5. Compilation & Installation Guide

### Prerequisites
* **Operating System**: Windows 10 or Windows 11.
* **C Compiler**: GCC (via MSYS2, MinGW-w64, or Git for Windows).
* **Hardware**: Standard laptop. No external hardware or microcontrollers required.
* **Dependencies**: None. Everything runs using standard C99 and Win32 system libraries (`-lws2_32`).

### Compiling from Source

Open PowerShell in `d:\AQUATWIN`:

1. **Compile the Core Simulator Server**:
   ```powershell
   gcc -Wall -Wextra src/engine.c src/server.c src/main.c -o aquatwin.exe -lws2_32 -lwinmm
   ```

2. **Compile and Run the Automated Test Suite**:
   ```powershell
   gcc -Wall -Wextra src/engine.c tests/test_engine.c -o tests/test_engine.exe
   .\tests\test_engine.exe
   ```

3. **Compile the Legacy Baseline (Original PDF Program)**:
   ```powershell
   gcc -Wall -Wextra legacy/water_monitor_legacy.c -o legacy/water_monitor_legacy.exe
   ```

---

## 6. How to Run AquaTwin

### Step 1: Launch the C Engine Server
In PowerShell:
```powershell
.\aquatwin.exe
```
Output:
```
====================================================================
   AQUATWIN: 3D SMART WATER DIGITAL TWIN (C SIMULATION SERVER)      
====================================================================
  [STATUS] Physics Engine Active (20 Hz mass conservation loop)
  [STATUS] HTTP Server listening at: http://localhost:8080/
  [STATUS] Web root directory:       web
  [STATUS] Open your browser at:     http://localhost:8080
====================================================================
  Press Ctrl+C to stop the simulator server.
```

### Step 2: Open the 3D Dashboard
Open any browser (Chrome, Edge, Firefox, Brave) and navigate to:
```
http://localhost:8080
```
* **Offline Operation**: The application is 100% self-contained. No internet connection is needed.

---

## 7. Automated Test Suite Results

The automated test runner (`tests/test_engine.exe`) executes 8 deterministic tests validating every engineering constraint:

```
====================================================================
  AQUATWIN: C SIMULATION ENGINE AUTOMATED TEST SUITE                
====================================================================
[RUN] Test 1: Mass Conservation & Integration Accuracy...
  [PASS] Mass conservation validated: 300L -> 400L over 10.0s (exact net 10 L/s).
[RUN] Test 2: Safe Boundary Clamping (0L to 1000L)...
  [PASS] Clamping validated: strictly bounded within [0.0L, 1000.0L].
[RUN] Test 3: PDF Threshold Classification & Exact Alerts...
  [PASS] Original PDF alerts and states verified 100% identical to specification.
[RUN] Test 4: Smart Controller Hysteresis (28% Start, 80% Stop)...
  [PASS] Dual-threshold hysteresis prevents chatter across the 28%-80% deadband.
[RUN] Test 5: Safety Interlock (Emergency Cutoff at 95%)...
  [PASS] Emergency safety interlock successfully trips pump OFF at >= 95%.
[RUN] Test 6: Engineering Scenarios & ENTC Signal Filtering...
  [PASS] Scenarios validated: Pump stall caught, Pipe leak active, EMA filter dampened noise (Raw peak: 7.97% vs Filtered peak: 3.26%).
[RUN] Test 7: JSON Telemetry Serialization...
  [PASS] JSON telemetry payload generated and verified.
[RUN] Test 8: Immediate Actuator Sync, Pause/Resume & Reset...
  [PASS] Actuator flow synchronization, pause/resume freeze, and reset validated.
====================================================================
  TEST RESULTS: 8 Passed, 0 Failed
====================================================================
```

---

## 8. Engineering Scenarios Guide

AquaTwin provides 6 selectable engineering scenarios to demonstrate physical dynamics and fault handling:

1. **Scenario 1: Normal Fill**
   * Feed pump actively charges the tank ($Q_{\text{in}} = 15\text{ L/s}$), base drain is light ($Q_{\text{out}} = 1.5\text{ L/s}$). Net accumulation is positive.
2. **Scenario 2: Water Demand (Consumer Consumption)**
   * Drain valve opens to $85\%$ ($Q_{\text{out}} = 8.5\text{ L/s}$). Tank level falls steadily, demonstrating depletion dynamics.
3. **Scenario 3: Overflow Hazard Injection**
   * Operator forces pump ON with drain closed. As water crosses $95\%$, the **Emergency Safety Interlock** trips the pump OFF.
4. **Scenario 4: Pump Actuator Failure**
   * Simulates an electrical motor stall or impeller jam. The controller requests the pump ON, but physical flow drops to $0\text{ L/s}$. The dashboard marks the pump LED red and flags an actuator fault.
5. **Scenario 5: Pipeline Rupture / Leak**
   * Injects an unaccounted $7.5\text{ L/s}$ subterranean leak. Demonstrates mass-balance discrepancy detection.
6. **Scenario 6: Sensor Noise & ENTC EMA Filter Analysis**
   * Transducer signal is corrupted with high-amplitude noise ($\pm 8\%$). The dashboard compares Ground Truth, Raw Transducer, and Filtered EMA signals side-by-side on the real-time canvas chart.

---

## 9. Project Demonstration Script (For Academic Presentations)

When presenting AquaTwin to professors, evaluators, or colleagues:

1. **Introduction (1 min)**:
   * Show the original PDF code in `legacy/water_monitor_legacy.c`. Explain how the simple static $0–100\%$ classification has been transformed into a full physics-backed digital twin while keeping C as the authoritative simulation engine.
2. **Architecture Demonstration (1.5 min)**:
   * Run `.\aquatwin.exe`. Explain that the C engine executes at $20\text{ Hz}$ mass conservation, serving telemetry over native Winsock2 without Python or Node.js.
   * Open `http://localhost:8080/`. Show the 3D tank, glass cylinder, and rotating pump.
3. **Hysteresis & Automation (1.5 min)**:
   * Show the system running in `AUTO` mode.
   * Highlight how the pump stays idle until the level drops to $\le 28\%$, starts automatically, fills up to $80\%$, and shuts down without rapid ON/OFF oscillation.
4. **Safety Interlock (1 min)**:
   * Switch to `MANUAL` mode. Click `Pump ON` and close the drain.
   * Watch the level rise past $95\%$. Observe the C safety interlock automatically disengaging the pump to prevent physical tank rupture.
5. **ENTC Transducer Filtering (1 min)**:
   * Click `Scenario 6: Sensor Noise`.
   * Direct attention to the live multi-trace chart showing how the Exponential Moving Average filter attenuates sensor noise while tracking ground truth.

---

## 10. Troubleshooting Guide

| Issue | Cause | Resolution |
| :--- | :--- | :--- |
| `Socket bind to port 8080 failed: 10048` | Another application is already using port 8080. | Launch AquaTwin on an alternate port: `.\aquatwin.exe --port 9090` and open `http://localhost:9090`. |
| Browser shows `C ENGINE: OFFLINE` | The C executable `aquatwin.exe` is not currently running. | Open a terminal and run `.\aquatwin.exe`. |
| 3D canvas is black | WebGL hardware acceleration disabled in browser. | In browser settings, enable "Hardware Acceleration", or try Edge/Chrome. |
| Pump won't turn ON when clicking button | System is in `AUTO` mode. | Click the `MANUAL` button first to enable manual operator overrides. |

---

## 11. Future Hardware Integration Scope

*(Clearly separated from the 100% laptop software implementation)*

When upgrading from pure software simulation to physical benchtop hardware:
* **Microcontroller Interface**: Replace the simulated time loop with an ESP32 or STM32 running the identical `engine.c` control logic.
* **Transducers**: Connect a physical ultrasonic sensor (HC-SR04 / JSN-SR04T) via GPIO timer input capture or a $4–20\text{ mA}$ hydrostatic pressure transmitter via ADC.
* **Actuators**: Connect a $12\text{V}$ DC mini-submersible pump via an optocoupled relay or MOSFET driver.
* **Telemetry**: Use MQTT or UART to stream JSON telemetry packets matching `engine_export_json()` directly into the AquaTwin 3D dashboard.
