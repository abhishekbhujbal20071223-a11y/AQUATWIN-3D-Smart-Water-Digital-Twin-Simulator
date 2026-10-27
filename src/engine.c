/**
 * ============================================================================
 * AQUATWIN: 3D Smart Water Monitoring and Control Simulator
 * Module: engine.c (Core Simulation & Physics Implementation)
 * ============================================================================
 */

#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void engine_get_default_config(TankConfig *tank_cfg, ThresholdConfig *thresh_cfg)
{
    if (tank_cfg) {
        tank_cfg->max_capacity_liters = 1000.0; /* 1000 Liters = 1 m^3 */
        tank_cfg->tank_height_meters = 2.0;
        tank_cfg->tank_radius_meters = 0.4;
        tank_cfg->max_pump_flow_lps = 15.0;     /* 15 L/s pump delivery */
        tank_cfg->max_drain_flow_lps = 10.0;    /* 10 L/s max drain flow */
    }

    if (thresh_cfg) {
        /* Preserves exact thresholds from original PDF baseline */
        thresh_cfg->low_threshold_pct = 30.0;   /* < 30% is LOW */
        thresh_cfg->high_threshold_pct = 80.0;  /* >= 80% is HIGH */
        thresh_cfg->overflow_risk_pct = 95.0;   /* >= 95% is OVERFLOW RISK */
        thresh_cfg->auto_start_level_pct = 28.0; /* Hysteresis lower threshold */
        thresh_cfg->auto_stop_level_pct = 80.0;  /* Hysteresis upper threshold */
        thresh_cfg->filter_alpha = 0.20;         /* ENTC low-pass filter smoothing */
    }
}

void engine_init(SimEngine *engine, const TankConfig *tank_cfg, const ThresholdConfig *thresh_cfg)
{
    if (!engine) return;
    memset(engine, 0, sizeof(SimEngine));

    if (tank_cfg) {
        engine->tank_cfg = *tank_cfg;
    } else {
        TankConfig def_t;
        engine_get_default_config(&def_t, NULL);
        engine->tank_cfg = def_t;
    }

    if (thresh_cfg) {
        engine->thresh_cfg = *thresh_cfg;
    } else {
        ThresholdConfig def_th;
        engine_get_default_config(NULL, &def_th);
        engine->thresh_cfg = def_th;
    }

    /* Initial state: 450 Liters (45.0% - normal operating zone) */
    engine->actual_volume_liters = 450.0;
    engine->actual_level_pct = (engine->actual_volume_liters / engine->tank_cfg.max_capacity_liters) * 100.0;
    engine->raw_sensor_level_pct = engine->actual_level_pct;
    engine->filtered_level_pct = engine->actual_level_pct;

    engine->sim_time_sec = 0.0;
    engine->dt_sec = 0.1;
    engine->is_paused = 0;

    engine->mode = MODE_AUTO;
    engine->pump_commanded = 0;
    engine->pump_actual_running = 0;
    engine->inflow_valve_pct = 100.0; /* Fully open */
    engine->drain_valve_pct = 20.0;   /* Light base consumption */

    engine->current_scenario = SCENARIO_NORMAL_FILL;
    snprintf(engine->controller_reason, sizeof(engine->controller_reason),
             "System initialized. Monitoring tank level.");
    snprintf(engine->fault_description, sizeof(engine->fault_description),
             "Nominal operation. No faults detected.");

    /* Initial classification */
    engine_step(engine, 0.0);
}

void engine_reset(SimEngine *engine)
{
    if (!engine) return;
    TankConfig t = engine->tank_cfg;
    ThresholdConfig th = engine->thresh_cfg;
    engine_init(engine, &t, &th);
}

void engine_set_mode(SimEngine *engine, ControlMode mode)
{
    if (!engine) return;
    engine->mode = mode;
    if (mode == MODE_AUTO) {
        snprintf(engine->controller_reason, sizeof(engine->controller_reason),
                 "Switched to AUTOMATIC closed-loop control.");
    } else {
        snprintf(engine->controller_reason, sizeof(engine->controller_reason),
                 "Switched to MANUAL operator override.");
    }
}

void engine_set_pump(SimEngine *engine, int pump_on)
{
    if (!engine) return;
    engine->pump_commanded = (pump_on != 0) ? 1 : 0;
    if (engine->current_scenario == SCENARIO_PUMP_FAILURE) {
        engine->pump_actual_running = 0;
        engine->inflow_rate_lps = 0.0;
    } else {
        engine->pump_actual_running = engine->pump_commanded;
        engine->inflow_rate_lps = engine->pump_actual_running ?
            (engine->tank_cfg.max_pump_flow_lps * (engine->inflow_valve_pct / 100.0)) : 0.0;
    }
    snprintf(engine->controller_reason, sizeof(engine->controller_reason),
             "Operator manually set pump to %s.", engine->pump_commanded ? "ON" : "OFF");
}

void engine_set_inflow_valve(SimEngine *engine, double pct)
{
    if (!engine) return;
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    engine->inflow_valve_pct = pct;
    if (engine->pump_actual_running) {
        engine->inflow_rate_lps = engine->tank_cfg.max_pump_flow_lps * (engine->inflow_valve_pct / 100.0);
    } else {
        engine->inflow_rate_lps = 0.0;
    }
}

void engine_set_drain_valve(SimEngine *engine, double pct)
{
    if (!engine) return;
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    engine->drain_valve_pct = pct;
    if (engine->actual_volume_liters > 0.001) {
        double consumer_flow = engine->tank_cfg.max_drain_flow_lps * (engine->drain_valve_pct / 100.0);
        engine->outflow_rate_lps = consumer_flow + engine->leak_rate_lps;
    }
}

void engine_set_scenario(SimEngine *engine, ScenarioType scenario)
{
    if (!engine) return;
    engine->current_scenario = scenario;

    switch (scenario) {
        case SCENARIO_NORMAL_FILL:
            engine->drain_valve_pct = 15.0;
            engine->inflow_valve_pct = 100.0;
            engine->leak_rate_lps = 0.0;
            snprintf(engine->fault_description, sizeof(engine->fault_description),
                     "Scenario 1: Normal filling cycle (Inflow > Outflow).");
            break;

        case SCENARIO_WATER_CONSUMPTION:
            engine->drain_valve_pct = 85.0; /* Heavy demand */
            engine->inflow_valve_pct = 100.0;
            engine->leak_rate_lps = 0.0;
            snprintf(engine->fault_description, sizeof(engine->fault_description),
                     "Scenario 2: Heavy consumer demand (High drain rate).");
            break;

        case SCENARIO_OVERFLOW_RISK:
            engine->drain_valve_pct = 0.0;
            engine->inflow_valve_pct = 100.0;
            engine->leak_rate_lps = 0.0;
            engine->mode = MODE_MANUAL;
            engine_set_pump(engine, 1);
            snprintf(engine->fault_description, sizeof(engine->fault_description),
                     "Scenario 3: Overflow hazard injection (Pump forced ON).");
            break;

        case SCENARIO_PUMP_FAILURE:
            engine->drain_valve_pct = 30.0;
            engine->leak_rate_lps = 0.0;
            engine->pump_commanded = 1;
            engine->pump_actual_running = 0;
            engine->inflow_rate_lps = 0.0;
            snprintf(engine->fault_description, sizeof(engine->fault_description),
                     "Scenario 4: Pump electrical/mechanical stall fault.");
            break;

        case SCENARIO_PIPE_LEAK:
            engine->drain_valve_pct = 20.0;
            engine->leak_rate_lps = 7.5;
            snprintf(engine->fault_description, sizeof(engine->fault_description),
                     "Scenario 5: Pipeline rupture (Unaccounted drain leakage).");
            break;

        case SCENARIO_SENSOR_NOISE:
            engine->leak_rate_lps = 0.0;
            snprintf(engine->fault_description, sizeof(engine->fault_description),
                     "Scenario 6: Transducer electrical EMI noise & filter evaluation.");
            break;
    }

    if (engine->actual_volume_liters > 0.001) {
        double consumer_flow = engine->tank_cfg.max_drain_flow_lps * (engine->drain_valve_pct / 100.0);
        engine->outflow_rate_lps = consumer_flow + engine->leak_rate_lps;
    }
}

void engine_set_paused(SimEngine *engine, int paused)
{
    if (!engine) return;
    engine->is_paused = paused ? 1 : 0;
}

void engine_step(SimEngine *engine, double dt_sec)
{
    if (!engine || engine->is_paused) return;
    if (dt_sec <= 0.0 && engine->sim_time_sec > 0.0) return;

    /* Advance simulation time */
    engine->sim_time_sec += dt_sec;

    /* ------------------------------------------------------------------------
     * 1. Scenario-dependent Actuator & Fault Handling
     * ------------------------------------------------------------------------ */
    if (engine->current_scenario == SCENARIO_PUMP_FAILURE) {
        /* Pump is commanded ON, but physical pump cannot pump liquid */
        engine->pump_actual_running = 0;
        engine->inflow_rate_lps = 0.0;
    } else {
        engine->pump_actual_running = engine->pump_commanded;
        if (engine->pump_actual_running) {
            engine->inflow_rate_lps = engine->tank_cfg.max_pump_flow_lps * (engine->inflow_valve_pct / 100.0);
        } else {
            engine->inflow_rate_lps = 0.0;
        }
    }

    /* Base consumer drain flow */
    double consumer_flow = engine->tank_cfg.max_drain_flow_lps * (engine->drain_valve_pct / 100.0);

    /* Leakage fault modeling */
    if (engine->current_scenario == SCENARIO_PIPE_LEAK) {
        engine->leak_rate_lps = 7.5; /* 7.5 L/s unaccounted pipe loss */
    } else {
        engine->leak_rate_lps = 0.0;
    }

    /* Outflow is constrained by water availability (cannot drain empty tank) */
    if (engine->actual_volume_liters <= 0.001) {
        engine->outflow_rate_lps = 0.0;
    } else {
        engine->outflow_rate_lps = consumer_flow + engine->leak_rate_lps;
    }

    /* ------------------------------------------------------------------------
     * 2. Physical Water Mass Balance: dV = (Q_in - Q_out) * dt
     * ------------------------------------------------------------------------ */
    double net_flow_lps = engine->inflow_rate_lps - engine->outflow_rate_lps;
    double delta_v = net_flow_lps * dt_sec;
    engine->actual_volume_liters += delta_v;

    /* Safe physical boundary clamping */
    if (engine->actual_volume_liters < 0.0) {
        engine->actual_volume_liters = 0.0;
    } else if (engine->actual_volume_liters > engine->tank_cfg.max_capacity_liters) {
        engine->actual_volume_liters = engine->tank_cfg.max_capacity_liters;
    }

    /* Calculate authoritative percentage from actual simulated volume */
    engine->actual_level_pct = (engine->actual_volume_liters / engine->tank_cfg.max_capacity_liters) * 100.0;

    /* ------------------------------------------------------------------------
     * 3. ENTC Sensor Modeling & Digital Signal Filtering
     * ------------------------------------------------------------------------ */
    double noise = 0.0;
    if (engine->current_scenario == SCENARIO_SENSOR_NOISE) {
        /* High transducer EMI noise: uniform distribution +/- 8.0% */
        noise = (((double)rand() / (double)RAND_MAX) * 16.0) - 8.0;
    } else {
        /* Nominal ADC quantization / ripple: +/- 0.4% */
        noise = (((double)rand() / (double)RAND_MAX) * 0.8) - 0.4;
    }

    engine->raw_sensor_level_pct = engine->actual_level_pct + noise;
    if (engine->raw_sensor_level_pct < 0.0) engine->raw_sensor_level_pct = 0.0;
    if (engine->raw_sensor_level_pct > 105.0) engine->raw_sensor_level_pct = 105.0;

    /* Exponential Moving Average (EMA) Low-Pass Filter:
     *   y[k] = alpha * x[k] + (1 - alpha) * y[k-1]
     */
    double alpha = engine->thresh_cfg.filter_alpha;
    engine->filtered_level_pct = (alpha * engine->raw_sensor_level_pct) +
                                 ((1.0 - alpha) * engine->filtered_level_pct);

    /* ------------------------------------------------------------------------
     * 4. State Classification (Preserving exact logic & messages from original PDF)
     * ------------------------------------------------------------------------ */
    double eval_level = engine->filtered_level_pct;

    if (eval_level < engine->thresh_cfg.low_threshold_pct) {
        engine->level_state = LEVEL_LOW;
        snprintf(engine->status_label, sizeof(engine->status_label), "LOW WATER LEVEL");
        /* Exact alert from PDF Page 4 & Page 6 */
        snprintf(engine->alert_message, sizeof(engine->alert_message),
                 "Alert: Check water supply and save water.");
    }
    else if (eval_level < engine->thresh_cfg.high_threshold_pct) {
        engine->level_state = LEVEL_MEDIUM;
        snprintf(engine->status_label, sizeof(engine->status_label), "MEDIUM WATER LEVEL");
        /* Exact message from PDF Page 4 & Page 7 */
        snprintf(engine->alert_message, sizeof(engine->alert_message),
                 "Message: Water level is in normal range.");
    }
    else if (eval_level < engine->thresh_cfg.overflow_risk_pct) {
        engine->level_state = LEVEL_HIGH;
        snprintf(engine->status_label, sizeof(engine->status_label), "HIGH WATER LEVEL");
        /* Exact alert from PDF Page 4 & Page 8 */
        snprintf(engine->alert_message, sizeof(engine->alert_message),
                 "Alert: Stop filling to avoid overflow.");
    }
    else {
        engine->level_state = LEVEL_OVERFLOW_RISK;
        snprintf(engine->status_label, sizeof(engine->status_label), "OVERFLOW RISK");
        snprintf(engine->alert_message, sizeof(engine->alert_message),
                 "CRITICAL: Tank capacity near 100%%! Emergency cutoff active.");
    }

    /* ------------------------------------------------------------------------
     * 5. Smart Controller & Safety Interlocks
     * ------------------------------------------------------------------------ */
    /* Safety Interlock: Hard cutoff at overflow risk threshold */
    if (engine->actual_level_pct >= engine->thresh_cfg.overflow_risk_pct) {
        if (engine->pump_commanded) {
            engine->pump_commanded = 0;
            engine->pump_actual_running = 0;
            snprintf(engine->controller_reason, sizeof(engine->controller_reason),
                     "SAFETY INTERLOCK: Forced pump OFF at >= %.1f%% overflow threshold.",
                     engine->thresh_cfg.overflow_risk_pct);
        }
    }
    /* Automatic Mode with Hysteresis (prevents chattering) */
    else if (engine->mode == MODE_AUTO) {
        if (eval_level <= engine->thresh_cfg.auto_start_level_pct) {
            if (!engine->pump_commanded) {
                engine->pump_commanded = 1;
                snprintf(engine->controller_reason, sizeof(engine->controller_reason),
                         "Auto Controller: Level <= %.1f%%. Pump started.",
                         engine->thresh_cfg.auto_start_level_pct);
            }
        }
        else if (eval_level >= engine->thresh_cfg.auto_stop_level_pct) {
            if (engine->pump_commanded) {
                engine->pump_commanded = 0;
                snprintf(engine->controller_reason, sizeof(engine->controller_reason),
                         "Auto Controller: Level >= %.1f%%. Pump stopped.",
                         engine->thresh_cfg.auto_stop_level_pct);
            }
        }
        /* Between auto_start and auto_stop: retain current pump state (deadband) */
    }
}

int engine_export_json(const SimEngine *engine, char *out_buf, size_t max_len)
{
    if (!engine || !out_buf || max_len == 0) return -1;

    int written = snprintf(out_buf, max_len,
        "{\n"
        "  \"sim_time_sec\": %.2f,\n"
        "  \"is_paused\": %d,\n"
        "  \"actual_volume_liters\": %.2f,\n"
        "  \"max_capacity_liters\": %.2f,\n"
        "  \"actual_level_pct\": %.2f,\n"
        "  \"raw_sensor_level_pct\": %.2f,\n"
        "  \"filtered_level_pct\": %.2f,\n"
        "  \"inflow_rate_lps\": %.2f,\n"
        "  \"outflow_rate_lps\": %.2f,\n"
        "  \"leak_rate_lps\": %.2f,\n"
        "  \"mode\": %d,\n"
        "  \"mode_label\": \"%s\",\n"
        "  \"pump_commanded\": %d,\n"
        "  \"pump_actual_running\": %d,\n"
        "  \"inflow_valve_pct\": %.1f,\n"
        "  \"drain_valve_pct\": %.1f,\n"
        "  \"level_state\": %d,\n"
        "  \"status_label\": \"%s\",\n"
        "  \"alert_message\": \"%s\",\n"
        "  \"controller_reason\": \"%s\",\n"
        "  \"current_scenario\": %d,\n"
        "  \"fault_description\": \"%s\"\n"
        "}",
        engine->sim_time_sec,
        engine->is_paused,
        engine->actual_volume_liters,
        engine->tank_cfg.max_capacity_liters,
        engine->actual_level_pct,
        engine->raw_sensor_level_pct,
        engine->filtered_level_pct,
        engine->inflow_rate_lps,
        engine->outflow_rate_lps,
        engine->leak_rate_lps,
        (int)engine->mode,
        engine->mode == MODE_AUTO ? "AUTO" : "MANUAL",
        engine->pump_commanded,
        engine->pump_actual_running,
        engine->inflow_valve_pct,
        engine->drain_valve_pct,
        (int)engine->level_state,
        engine->status_label,
        engine->alert_message,
        engine->controller_reason,
        (int)engine->current_scenario,
        engine->fault_description
    );

    return (written > 0 && (size_t)written < max_len) ? 0 : -1;
}
