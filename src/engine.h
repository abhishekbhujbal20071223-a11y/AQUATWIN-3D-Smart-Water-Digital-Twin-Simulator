/**
 * ============================================================================
 * AQUATWIN: 3D Smart Water Monitoring and Control Simulator
 * Module: engine.h (Core Simulation Engine Header)
 * ============================================================================
 * Preserves & extends the original "Water Level Monitoring System" logic
 * with physical mass conservation, hysteresis control, ENTC signal filtering,
 * and safety interlocks.
 * ============================================================================
 */

#ifndef AQUATWIN_ENGINE_H
#define AQUATWIN_ENGINE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Water level classification states (matches & extends original PDF logic) */
typedef enum {
    LEVEL_LOW = 0,         /* 0% to 29.9%: LOW WATER LEVEL */
    LEVEL_MEDIUM = 1,      /* 30% to 79.9%: MEDIUM WATER LEVEL */
    LEVEL_HIGH = 2,        /* 80% to 94.9%: HIGH WATER LEVEL */
    LEVEL_OVERFLOW_RISK = 3 /* 95% to 100%: OVERFLOW RISK (Safety Emergency) */
} WaterLevelState;

/* Operational control modes */
typedef enum {
    MODE_AUTO = 0,   /* Closed-loop hysteresis feedback controller */
    MODE_MANUAL = 1  /* Operator manual override */
} ControlMode;

/* Predefined engineering simulation scenarios */
typedef enum {
    SCENARIO_NORMAL_FILL = 0,        /* Standard filling cycle */
    SCENARIO_WATER_CONSUMPTION = 1,  /* High outflow (consumer demand) */
    SCENARIO_OVERFLOW_RISK = 2,      /* Inflow exceeds high threshold */
    SCENARIO_PUMP_FAILURE = 3,       /* Actuator failure: pump ON but Q_in = 0 */
    SCENARIO_PIPE_LEAK = 4,          /* Pipeline rupture: unexpected outflow */
    SCENARIO_SENSOR_NOISE = 5        /* Faulty transducer / high electrical EMI */
} ScenarioType;

/* Physical tank configuration */
typedef struct {
    double max_capacity_liters;   /* e.g., 1000.0 L */
    double tank_height_meters;     /* e.g., 2.0 m */
    double tank_radius_meters;     /* e.g., 0.4 m */
    double max_pump_flow_lps;      /* Max pump delivery rate (Liters/second) */
    double max_drain_flow_lps;     /* Max gravity drain rate (Liters/second) */
} TankConfig;

/* Threshold and control setpoint configuration */
typedef struct {
    double low_threshold_pct;      /* 30.0% (from PDF: <30% is LOW) */
    double high_threshold_pct;     /* 80.0% (from PDF: >=80% is HIGH) */
    double overflow_risk_pct;      /* 95.0% (Emergency safety interlock cutoff) */
    double auto_start_level_pct;   /* Hysteresis: start filling at <= 28.0% */
    double auto_stop_level_pct;    /* Hysteresis: stop filling at >= 80.0% */
    double filter_alpha;           /* ENTC filter smoothing factor: 0.0 < alpha <= 1.0 */
} ThresholdConfig;

/* Live telemetry and authoritative simulation state */
typedef struct {
    /* Time & Simulation status */
    double sim_time_sec;
    double dt_sec;                 /* Integration time-step */
    int is_paused;

    /* Physics & Hydraulics */
    double actual_volume_liters;   /* Ground truth volume (Liters) */
    double actual_level_pct;       /* Ground truth level (0.0 to 100.0%) */
    double inflow_rate_lps;        /* Current inlet flow (L/s) */
    double outflow_rate_lps;       /* Current consumer outlet flow (L/s) */
    double leak_rate_lps;          /* Unaccounted leak flow (L/s) */

    /* ENTC Instrumentation & Signal Conditioning */
    double raw_sensor_level_pct;   /* Unfiltered sensor reading (with noise) */
    double filtered_level_pct;     /* Low-pass filtered level (EMA) */
    double sensor_noise_amplitude; /* Active noise std deviation / amplitude */

    /* Actuators & Control */
    ControlMode mode;
    int pump_commanded;            /* 1 = ON, 0 = OFF (requested) */
    int pump_actual_running;       /* 1 = ON, 0 = OFF (physical reality) */
    double inflow_valve_pct;       /* Inflow valve opening (0 to 100%) */
    double drain_valve_pct;        /* Drain valve opening (0 to 100%) */

    /* State classification & Diagnostics (preserving PDF alerts) */
    WaterLevelState level_state;
    ScenarioType current_scenario;
    char status_label[32];         /* "LOW WATER LEVEL", "MEDIUM...", etc. */
    char alert_message[128];       /* PDF message: "Check water supply...", etc. */
    char controller_reason[128];   /* Reason for last controller state switch */
    char fault_description[128];   /* Fault diagnosis text */

    /* Configuration references */
    TankConfig tank_cfg;
    ThresholdConfig thresh_cfg;
} SimEngine;

/* Engine lifecycle & control API */
void engine_get_default_config(TankConfig *tank_cfg, ThresholdConfig *thresh_cfg);
void engine_init(SimEngine *engine, const TankConfig *tank_cfg, const ThresholdConfig *thresh_cfg);
void engine_reset(SimEngine *engine);
void engine_step(SimEngine *engine, double dt_sec);

/* Actuator & Mode Setters */
void engine_set_mode(SimEngine *engine, ControlMode mode);
void engine_set_pump(SimEngine *engine, int pump_on);
void engine_set_inflow_valve(SimEngine *engine, double pct);
void engine_set_drain_valve(SimEngine *engine, double pct);
void engine_set_scenario(SimEngine *engine, ScenarioType scenario);
void engine_set_paused(SimEngine *engine, int paused);

/* Telemetry serialization for web interface */
int engine_export_json(const SimEngine *engine, char *out_buf, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* AQUATWIN_ENGINE_H */
