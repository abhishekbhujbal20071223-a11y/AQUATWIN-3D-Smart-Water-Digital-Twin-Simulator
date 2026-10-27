/**
 * ============================================================================
 * AQUATWIN: 3D Smart Water Monitoring and Control Simulator
 * Module: test_engine.c (Automated Test Suite for C Simulation Engine)
 * ============================================================================
 */

#include "../src/engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define EPSILON 0.05

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

/* Helper: Set exact water volume */
static void force_volume(SimEngine *e, double liters) {
    e->actual_volume_liters = liters;
    e->actual_level_pct = (liters / e->tank_cfg.max_capacity_liters) * 100.0;
    e->raw_sensor_level_pct = e->actual_level_pct;
    e->filtered_level_pct = e->actual_level_pct;
}

/* -------------------------------------------------------------------------
 * TEST 1: Physical Mass Balance (dV = (Q_in - Q_out) * dt)
 * ------------------------------------------------------------------------- */
static void test_mass_conservation(void)
{
    printf("[RUN] Test 1: Mass Conservation & Integration Accuracy...\n");
    SimEngine engine;
    engine_init(&engine, NULL, NULL);

    /* Manual mode: constant inflow 15 L/s, drain 5 L/s -> net +10 L/s */
    engine_set_mode(&engine, MODE_MANUAL);
    engine_set_pump(&engine, 1);
    engine_set_inflow_valve(&engine, 100.0); /* 15 L/s */
    engine_set_drain_valve(&engine, 50.0);   /* 5 L/s */
    force_volume(&engine, 300.0);            /* Start at 300 L */

    /* Run 10.0 seconds with dt = 0.1s (100 steps) */
    for (int i = 0; i < 100; i++) {
        engine_step(&engine, 0.1);
    }

    /* Expected: 300 + 10 * 10 = 400.0 Liters (40.0%) */
    TEST_ASSERT(fabs(engine.actual_volume_liters - 400.0) < EPSILON,
                "Volume does not match theoretical mass balance calculation");
    TEST_ASSERT(fabs(engine.actual_level_pct - 40.0) < EPSILON,
                "Percentage does not match theoretical level calculation");

    printf("  [PASS] Mass conservation validated: 300L -> 400L over 10.0s (exact net 10 L/s).\n");
    g_tests_passed++;
}

/* -------------------------------------------------------------------------
 * TEST 2: Safe Boundary Clamping (Zero & Max Capacity Limits)
 * ------------------------------------------------------------------------- */
static void test_boundary_clamping(void)
{
    printf("[RUN] Test 2: Safe Boundary Clamping (0L to 1000L)...\n");
    SimEngine engine;
    engine_init(&engine, NULL, NULL);
    engine_set_mode(&engine, MODE_MANUAL);

    /* Test 2A: Overfilling beyond capacity */
    force_volume(&engine, 990.0);
    engine_set_pump(&engine, 1);
    engine_set_inflow_valve(&engine, 100.0);
    engine_set_drain_valve(&engine, 0.0);

    /* Force step past 1000L */
    for (int i = 0; i < 50; i++) {
        engine_step(&engine, 0.1);
    }
    TEST_ASSERT(engine.actual_volume_liters <= 1000.0, "Volume exceeded 1000L capacity!");
    TEST_ASSERT(engine.actual_level_pct <= 100.0, "Level percentage exceeded 100%!");

    /* Test 2B: Draining below zero */
    engine_set_pump(&engine, 0);
    engine_set_drain_valve(&engine, 100.0);
    for (int i = 0; i < 2000; i++) {
        engine_step(&engine, 0.1);
    }
    TEST_ASSERT(engine.actual_volume_liters >= 0.0, "Volume dipped below 0L!");
    TEST_ASSERT(engine.actual_level_pct >= 0.0, "Level percentage dipped below 0%!");
    TEST_ASSERT(engine.outflow_rate_lps == 0.0, "Empty tank should have 0 L/s outflow!");

    printf("  [PASS] Clamping validated: strictly bounded within [0.0L, 1000.0L].\n");
    g_tests_passed++;
}

/* -------------------------------------------------------------------------
 * TEST 3: Preservation of Original PDF Logic & Alert Classification
 * ------------------------------------------------------------------------- */
static void test_pdf_classification_and_alerts(void)
{
    printf("[RUN] Test 3: PDF Threshold Classification & Exact Alerts...\n");
    SimEngine engine;
    engine_init(&engine, NULL, NULL);

    /* Case A: 20% (From PDF Slide 6) */
    force_volume(&engine, 200.0);
    engine_step(&engine, 0.0);
    TEST_ASSERT(engine.level_state == LEVEL_LOW, "State should be LOW for 20%");
    TEST_ASSERT(strcmp(engine.status_label, "LOW WATER LEVEL") == 0, "Status label mismatch for LOW");
    TEST_ASSERT(strcmp(engine.alert_message, "Alert: Check water supply and save water.") == 0,
                "Alert message mismatch for LOW (PDF Slide 4/6)");

    /* Case B: 55% (From PDF Slide 7) */
    force_volume(&engine, 550.0);
    engine_step(&engine, 0.0);
    TEST_ASSERT(engine.level_state == LEVEL_MEDIUM, "State should be MEDIUM for 55%");
    TEST_ASSERT(strcmp(engine.status_label, "MEDIUM WATER LEVEL") == 0, "Status label mismatch for MEDIUM");
    TEST_ASSERT(strcmp(engine.alert_message, "Message: Water level is in normal range.") == 0,
                "Message mismatch for MEDIUM (PDF Slide 4/7)");

    /* Case C: 90% (From PDF Slide 8) */
    force_volume(&engine, 900.0);
    engine_step(&engine, 0.0);
    TEST_ASSERT(engine.level_state == LEVEL_HIGH, "State should be HIGH for 90%");
    TEST_ASSERT(strcmp(engine.status_label, "HIGH WATER LEVEL") == 0, "Status label mismatch for HIGH");
    TEST_ASSERT(strcmp(engine.alert_message, "Alert: Stop filling to avoid overflow.") == 0,
                "Alert message mismatch for HIGH (PDF Slide 4/8)");

    /* Case D: 96% (Extended Overflow Risk) */
    force_volume(&engine, 960.0);
    engine_step(&engine, 0.0);
    TEST_ASSERT(engine.level_state == LEVEL_OVERFLOW_RISK, "State should be OVERFLOW RISK for 96%");

    printf("  [PASS] Original PDF alerts and states verified 100%% identical to specification.\n");
    g_tests_passed++;
}

/* -------------------------------------------------------------------------
 * TEST 4: Smart Controller Hysteresis & Anti-Chattering
 * ------------------------------------------------------------------------- */
static void test_smart_hysteresis_control(void)
{
    printf("[RUN] Test 4: Smart Controller Hysteresis (28%% Start, 80%% Stop)...\n");
    SimEngine engine;
    engine_init(&engine, NULL, NULL);
    engine_set_mode(&engine, MODE_AUTO);

    /* 1. Start with water at 25% (below 28% low threshold) -> Pump MUST turn ON */
    force_volume(&engine, 250.0);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.pump_commanded == 1, "Auto controller should turn pump ON when <= 28%");

    /* 2. Water rises into intermediate deadband: 40%, 60%, 75% -> Pump MUST STAY ON */
    force_volume(&engine, 500.0);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.pump_commanded == 1, "Pump must remain ON in hysteresis deadband (50%)");

    force_volume(&engine, 750.0);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.pump_commanded == 1, "Pump must remain ON in hysteresis deadband (75%)");

    /* 3. Water reaches 80% (high threshold) -> Pump MUST turn OFF */
    force_volume(&engine, 810.0);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.pump_commanded == 0, "Auto controller should turn pump OFF when >= 80%");

    /* 4. Water falls back into deadband: 70%, 50%, 35% -> Pump MUST STAY OFF */
    force_volume(&engine, 650.0);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.pump_commanded == 0, "Pump must remain OFF while draining in deadband (65%)");

    force_volume(&engine, 350.0);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.pump_commanded == 0, "Pump must remain OFF while draining in deadband (35%)");

    /* 5. Water falls below 28% -> Pump turns ON again */
    force_volume(&engine, 240.0);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.pump_commanded == 1, "Pump must turn ON again once level dips <= 28%");

    printf("  [PASS] Dual-threshold hysteresis prevents chatter across the 28%%-80%% deadband.\n");
    g_tests_passed++;
}

/* -------------------------------------------------------------------------
 * TEST 5: Safety Interlock Override
 * ------------------------------------------------------------------------- */
static void test_safety_interlock(void)
{
    printf("[RUN] Test 5: Safety Interlock (Emergency Cutoff at 95%%)...\n");
    SimEngine engine;
    engine_init(&engine, NULL, NULL);

    /* Operator in MANUAL mode recklessly forces pump ON at 94% */
    engine_set_mode(&engine, MODE_MANUAL);
    force_volume(&engine, 940.0);
    engine_set_pump(&engine, 1);
    engine_set_inflow_valve(&engine, 100.0);
    engine_set_drain_valve(&engine, 0.0);

    /* Run simulation until level crosses 95.0% */
    for (int i = 0; i < 20; i++) {
        engine_step(&engine, 0.1);
    }

    /* Interlock should immediately trip the pump OFF */
    TEST_ASSERT(engine.actual_level_pct >= 95.0, "Level should have crossed 95%");
    TEST_ASSERT(engine.pump_commanded == 0, "Safety interlock failed to trip pump commanded to 0!");
    TEST_ASSERT(engine.pump_actual_running == 0, "Safety interlock failed to trip physical pump!");

    printf("  [PASS] Emergency safety interlock successfully trips pump OFF at >= 95%%.\n");
    g_tests_passed++;
}

/* -------------------------------------------------------------------------
 * TEST 6: Engineering Scenarios (Leak, Pump Fault, Noise Filtering)
 * ------------------------------------------------------------------------- */
static void test_scenarios_and_filtering(void)
{
    printf("[RUN] Test 6: Engineering Scenarios & ENTC Signal Filtering...\n");
    SimEngine engine;
    engine_init(&engine, NULL, NULL);

    /* 6A: Pump Failure Scenario */
    engine_set_scenario(&engine, SCENARIO_PUMP_FAILURE);
    engine_set_pump(&engine, 1);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.pump_commanded == 1, "Pump commanded was ON");
    TEST_ASSERT(engine.pump_actual_running == 0, "Physical pump should fail to deliver flow");
    TEST_ASSERT(engine.inflow_rate_lps == 0.0, "Inflow rate must be 0 L/s during pump failure");

    /* 6B: Pipe Leak Scenario */
    engine_set_scenario(&engine, SCENARIO_PIPE_LEAK);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.leak_rate_lps > 5.0, "Leak rate must be present in pipe leak scenario");

    /* 6C: Sensor Noise & EMA Low-Pass Filter */
    engine_set_scenario(&engine, SCENARIO_SENSOR_NOISE);
    force_volume(&engine, 500.0); /* 50.0% */
    engine_set_drain_valve(&engine, 0.0); /* Keep water volume steady to isolate noise */
    engine_set_pump(&engine, 0);

    double max_raw_noise = 0.0;
    double max_filt_noise = 0.0;

    for (int i = 0; i < 100; i++) {
        engine_step(&engine, 0.1);
        double raw_noise = fabs(engine.raw_sensor_level_pct - engine.actual_level_pct);
        double filt_noise = fabs(engine.filtered_level_pct - engine.actual_level_pct);
        if (raw_noise > max_raw_noise) max_raw_noise = raw_noise;
        if (filt_noise > max_filt_noise) max_filt_noise = filt_noise;
    }

    /* Filtered level noise must be substantially less than raw peak noise */
    TEST_ASSERT(max_raw_noise > 3.0, "Raw noise should produce noticeable spikes (>3%)");
    TEST_ASSERT(max_filt_noise < max_raw_noise * 0.65, "EMA filter failed to dampen noise!");

    printf("  [PASS] Scenarios validated: Pump stall caught, Pipe leak active, EMA filter dampened noise (Raw peak: %.2f%% vs Filtered peak: %.2f%%).\n",
           max_raw_noise, max_filt_noise);
    g_tests_passed++;
}

/* -------------------------------------------------------------------------
 * TEST 7: JSON Telemetry Serialization
 * ------------------------------------------------------------------------- */
static void test_json_telemetry(void)
{
    printf("[RUN] Test 7: JSON Telemetry Serialization...\n");
    SimEngine engine;
    engine_init(&engine, NULL, NULL);
    engine_set_drain_valve(&engine, 0.0); /* Zero drain so volume stays exact */
    force_volume(&engine, 550.0);
    engine_step(&engine, 0.0);

    char json[2048];
    int res = engine_export_json(&engine, json, sizeof(json));
    TEST_ASSERT(res == 0, "JSON export returned error code");
    TEST_ASSERT(strstr(json, "\"actual_volume_liters\": 550.00") != NULL, "JSON missing volume");
    TEST_ASSERT(strstr(json, "\"status_label\": \"MEDIUM WATER LEVEL\"") != NULL, "JSON missing status label");
    TEST_ASSERT(strstr(json, "\"alert_message\": \"Message: Water level is in normal range.\"") != NULL, "JSON missing PDF alert message");

    printf("  [PASS] JSON telemetry payload generated and verified.\n");
    g_tests_passed++;
}

/* -------------------------------------------------------------------------
 * TEST 8: Immediate Actuator Synchronization, Pause/Resume & Reset
 * ------------------------------------------------------------------------- */
static void test_actuator_sync_and_pause(void)
{
    printf("[RUN] Test 8: Immediate Actuator Sync, Pause/Resume & Reset...\n");
    SimEngine engine;
    engine_init(&engine, NULL, NULL);

    /* Test immediate pump & valve flow synchronization */
    engine_set_mode(&engine, MODE_MANUAL);
    engine_set_inflow_valve(&engine, 60.0);
    engine_set_pump(&engine, 1);
    TEST_ASSERT(engine.pump_actual_running == 1, "Pump should immediately report actual running");
    TEST_ASSERT(fabs(engine.inflow_rate_lps - 9.0) < EPSILON, "Inflow rate should be 60% of 15 L/s = 9.0 L/s");

    /* Test immediate drain valve synchronization */
    engine_set_drain_valve(&engine, 40.0);
    TEST_ASSERT(fabs(engine.outflow_rate_lps - 4.0) < EPSILON, "Outflow rate should be 40% of 10 L/s = 4.0 L/s");

    /* Test Pause functionality: simulation time and volume must freeze */
    double pre_pause_time = engine.sim_time_sec;
    double pre_pause_vol = engine.actual_volume_liters;
    engine_set_paused(&engine, 1);
    TEST_ASSERT(engine.is_paused == 1, "is_paused flag must be 1");

    for (int i = 0; i < 20; i++) {
        engine_step(&engine, 0.1);
    }
    TEST_ASSERT(engine.sim_time_sec == pre_pause_time, "Sim time must freeze while paused");
    TEST_ASSERT(engine.actual_volume_liters == pre_pause_vol, "Volume must freeze while paused");

    /* Test Resume functionality: simulation steps again */
    engine_set_paused(&engine, 0);
    engine_step(&engine, 0.1);
    TEST_ASSERT(engine.sim_time_sec > pre_pause_time, "Sim time must advance after resume");

    /* Test Reset functionality: returns to baseline 450L (45%) */
    engine_reset(&engine);
    TEST_ASSERT(fabs(engine.actual_volume_liters - 450.0) < EPSILON, "Reset must restore baseline 450L");
    TEST_ASSERT(engine.sim_time_sec == 0.0, "Reset must restore time 0.0s");

    printf("  [PASS] Actuator flow synchronization, pause/resume freeze, and reset validated.\n");
    g_tests_passed++;
}

/* -------------------------------------------------------------------------
 * Main Test Runner
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("====================================================================\n");
    printf("  AQUATWIN: C SIMULATION ENGINE AUTOMATED TEST SUITE                \n");
    printf("====================================================================\n");

    test_mass_conservation();
    test_boundary_clamping();
    test_pdf_classification_and_alerts();
    test_smart_hysteresis_control();
    test_safety_interlock();
    test_scenarios_and_filtering();
    test_json_telemetry();
    test_actuator_sync_and_pause();

    printf("====================================================================\n");
    printf("  TEST RESULTS: %d Passed, %d Failed\n", g_tests_passed, g_tests_failed);
    printf("====================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
