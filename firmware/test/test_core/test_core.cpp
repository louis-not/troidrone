#include <unity.h>
#include <cmath>
#include <cstring>

#include "flap_engine.h"
#include "battery.h"
#include "flight_fsm.h"
#include "protocol_codec.h"
#include "link.h"
#include "servo_ease.h"

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// 1. FlapEngine Tests
// ============================================================================

void test_flap_engine_defaults_and_deadband(void) {
    FlapEngine engine;

    // Verify initial default parameters
    const FlapParams& p = engine.params();
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, p.center_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 75.0f, p.amp_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, p.f_min_hz);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.0f, p.f_max_hz);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.05f, p.deadband);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, p.min_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 180.0f, p.max_deg);

    // Initial phase should be downstroke start (0.75)
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, engine.phase());

    // Disarmed with throttle -> neutral angles, 0 Hz, phase reset to 0.75
    FlapOutput out1 = engine.update(0.02f, false, 0.8f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, out1.freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, out1.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, out1.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, out1.right_deg);

    // Armed with 0 throttle -> neutral angles, 0 Hz, phase reset to 0.75
    FlapOutput out2 = engine.update(0.02f, true, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, out2.freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, out2.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, out2.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, out2.right_deg);

    // Armed with throttle at exact deadband threshold (0.05) -> still neutral
    FlapOutput out3 = engine.update(0.02f, true, 0.05f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, out3.freq_hz);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, out3.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, out3.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.0f, out3.right_deg);
}

void test_flap_engine_frequency_mapping(void) {
    FlapEngine engine;

    // Just above deadband -> f_min_hz (1.0 Hz)
    FlapOutput out_min = engine.update(0.0f, true, 0.05001f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, out_min.freq_hz);

    // Midpoint: thr = 0.525f -> (0.525 - 0.05) / 0.95 = 0.5 -> 1.0 + 0.5 * 3.0 = 2.5 Hz
    FlapOutput out_mid = engine.update(0.0f, true, 0.525f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, out_mid.freq_hz);

    // Full throttle: thr = 1.0f -> 4.0 Hz
    FlapOutput out_max = engine.update(0.0f, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, out_max.freq_hz);

    // Over-throttle clamp: thr = 1.5f -> clamped to 1.0 -> 4.0 Hz
    FlapOutput out_clamp = engine.update(0.0f, true, 1.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, out_clamp.freq_hz);
}

void test_flap_engine_triangle_wave_and_amplitude(void) {
    FlapEngine engine;

    // Phase progression at f = 4.0 Hz (period = 0.25 s = 250 ms)
    // At t=0 (phase starts at 0.75):
    // w = 1.0 - 4.0 * |0.75 - 0.5| = 1.0 - 4.0 * 0.25 = 0.0
    // left = 90.0, right = 90.0
    FlapOutput out0 = engine.update(0.0f, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.75f, out0.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, out0.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, out0.right_deg);

    // Step dt = 1/16 s = 0.0625 s -> phase advance = 4.0 * 0.0625 = 0.25
    // phase = 0.75 + 0.25 = 1.0 -> 0.0 (bottom of stroke)
    // w = 1.0 - 4.0 * |0.0 - 0.5| = 1.0 - 2.0 = -1.0
    // left = 90 - 75 = 15.0 deg, right = (180 - 90) - (-75) = 165.0 deg
    FlapOutput out1 = engine.update(0.0625f, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, out1.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 15.0f, out1.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 165.0f, out1.right_deg);

    // Step dt = 0.0625 s -> phase advance 0.25 -> phase = 0.25 (midpoint upward)
    // w = 1.0 - 4.0 * |0.25 - 0.5| = 1.0 - 1.0 = 0.0
    // left = 90.0 deg, right = 90.0 deg
    FlapOutput out2 = engine.update(0.0625f, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.25f, out2.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, out2.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, out2.right_deg);

    // Step dt = 0.0625 s -> phase advance 0.25 -> phase = 0.50 (peak upward)
    // w = 1.0 - 4.0 * |0.5 - 0.5| = +1.0
    // left = 90 + 75 = 165.0 deg, right = (180 - 90) - 75 = 15.0 deg
    FlapOutput out3 = engine.update(0.0625f, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.50f, out3.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 165.0f, out3.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 15.0f, out3.right_deg);

    // Step dt = 0.0625 s -> phase advance 0.25 -> phase = 0.75 (neutral downstroke start)
    // w = 0.0 -> left = 90.0 deg, right = 90.0 deg
    FlapOutput out4 = engine.update(0.0625f, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.75f, out4.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, out4.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, out4.right_deg);

    // Verify wing symmetry / mirroring: left_deg + right_deg == 180.0 across all steps
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 180.0f, out0.left_deg + out0.right_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 180.0f, out1.left_deg + out1.right_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 180.0f, out2.left_deg + out2.right_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 180.0f, out3.left_deg + out3.right_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 180.0f, out4.left_deg + out4.right_deg);
}

void test_flap_engine_phase_reset(void) {
    FlapEngine engine;

    // Advance phase
    engine.update(0.05f, true, 1.0f);
    TEST_ASSERT_TRUE(std::fabs(engine.phase() - 0.75f) > 0.01f);

    // Explicit reset()
    engine.reset();
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, engine.phase());

    // Advance again
    engine.update(0.05f, true, 1.0f);
    TEST_ASSERT_TRUE(std::fabs(engine.phase() - 0.75f) > 0.01f);

    // Cutting throttle below deadband also resets phase
    FlapOutput out = engine.update(0.01f, true, 0.02f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, out.phase);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, engine.phase());
}

void test_flap_engine_runtime_param_changes(void) {
    FlapEngine engine;

    FlapParams custom;
    custom.center_deg = 80.0f;
    custom.amp_deg = 60.0f;
    custom.f_min_hz = 2.0f;
    custom.f_max_hz = 5.0f;
    custom.deadband = 0.10f;
    custom.min_deg = 10.0f;
    custom.max_deg = 170.0f;

    engine.set_params(custom);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 80.0f, engine.params().center_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 60.0f, engine.params().amp_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, engine.params().f_min_hz);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, engine.params().f_max_hz);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.10f, engine.params().deadband);

    // Idle output matches custom center: left = 80, right = 180 - 80 = 100
    FlapOutput idle = engine.update(0.01f, false, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 80.0f, idle.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, idle.right_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, idle.freq_hz);

    // Full throttle: 5.0 Hz
    FlapOutput full = engine.update(0.0f, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.0f, full.freq_hz);

    // Step to peak (phase advance 0.75 -> 0.5 takes dt = 0.75 / 5.0 = 0.15 s)
    FlapOutput peak = engine.update(0.15f, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.50f, peak.phase);
    // left = 80 + 60 = 140, right = (180 - 80) - 60 = 40
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 140.0f, peak.left_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 40.0f, peak.right_deg);
}

void test_flap_engine_differential_mixer_and_idle_lean(void) {
    FlapEngine engine;

    // 1. Idle lean at zero throttle
    FlapOutput idle_out = engine.update(0.045f, false, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, idle_out.freq_hz);
    TEST_ASSERT_TRUE(idle_out.left_deg < 90.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, idle_out.left_deg, idle_out.right_deg);

    // 2. Differential steering when armed
    engine.reset();
    FlapOutput flap_straight = engine.update(0.0625f, true, 1.0f, 0.0f);
    engine.reset();
    FlapOutput flap_steer_right = engine.update(0.0625f, true, 1.0f, 1.0f);
    TEST_ASSERT_TRUE(flap_steer_right.left_deg != flap_straight.left_deg);
}

// ============================================================================
// BatteryMonitor Tests
// ============================================================================

void test_battery_monitor_divider_and_percentage(void) {
    // 51k / 20k divider ratio: 2000 mV -> 7.10 V pack
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 7.10f, BatteryMonitor::adc_mv_to_pack_v(2000));

    // LiPo percentage table test:
    TEST_ASSERT_EQUAL_UINT8(100, BatteryMonitor::cell_voltage_to_percentage(4.20f));
    TEST_ASSERT_EQUAL_UINT8(0, BatteryMonitor::cell_voltage_to_percentage(3.30f));
    TEST_ASSERT_EQUAL_UINT8(50, BatteryMonitor::cell_voltage_to_percentage(3.85f));
    TEST_ASSERT_EQUAL_UINT8(100, BatteryMonitor::cell_voltage_to_percentage(4.35f));
    TEST_ASSERT_EQUAL_UINT8(0, BatteryMonitor::cell_voltage_to_percentage(3.00f));
}

void test_battery_monitor_ema_filter_and_cutoff_latch(void) {
    BatteryMonitor bat;
    bat.update_pack_v(8.0f, 0.02f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 8.0f, bat.voltage());
    TEST_ASSERT_EQUAL(BatteryState::OK, bat.state());
    TEST_ASSERT_FALSE(bat.is_cutoff_latched());
    TEST_ASSERT_TRUE(bat.is_ok());

    // Step down to 7.3V
    bat.update_pack_v(7.3f, 0.02f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 7.9825f, bat.voltage());

    // Set instant fall alpha to test threshold timing
    BatteryParams p = bat.params();
    p.ema_fall_alpha = 1.0f;
    bat.set_params(p);
    bat.update_pack_v(6.35f, 0.5f);
    TEST_ASSERT_FALSE(bat.is_cutoff_latched());

    bat.update_pack_v(6.35f, 1.0f);
    TEST_ASSERT_FALSE(bat.is_cutoff_latched());

    bat.update_pack_v(6.35f, 0.6f);
    TEST_ASSERT_TRUE(bat.is_cutoff_latched());
    TEST_ASSERT_EQUAL(BatteryState::CUTOFF, bat.state());
    TEST_ASSERT_FALSE(bat.is_ok());

    // Latched until reboot
    bat.update_pack_v(8.4f, 1.0f);
    TEST_ASSERT_TRUE(bat.is_cutoff_latched());
    TEST_ASSERT_EQUAL(BatteryState::CUTOFF, bat.state());
}

// ============================================================================
// 2. FlightFSM Tests
// ============================================================================

void test_fsm_boot_state(void) {
    FlightFsm fsm;

    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());
    TEST_ASSERT_EQUAL_STRING("DISARMED", fsm.state_str());
    TEST_ASSERT_FALSE(fsm.has_ctl());
    TEST_ASSERT_EQUAL_UINT32(9999u, fsm.age_ms(1000));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, fsm.effective_thr());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, fsm.last_throttle());
}

void test_fsm_arm_denied_without_ctl(void) {
    FlightFsm fsm;

    // Arm requested without ever receiving ctl
    bool armed = fsm.on_arm(100, true);
    TEST_ASSERT_FALSE(armed);
    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());
}

void test_fsm_arm_denied_with_stale_ctl_age_gt_500(void) {
    FlightFsm fsm;

    fsm.on_ctl(100, 0.0f, 0.0f, 1);
    TEST_ASSERT_TRUE(fsm.has_ctl());

    // At t=601 ms: age = 501 ms (> 500 ms)
    bool armed = fsm.on_arm(601, true);
    TEST_ASSERT_FALSE(armed);
    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());
}

void test_fsm_arm_denied_with_thr_gte_005(void) {
    FlightFsm fsm;

    // Exactly at threshold 0.05
    fsm.on_ctl(100, 0.05f, 0.0f, 1);
    bool armed = fsm.on_arm(120, true);
    TEST_ASSERT_FALSE(armed);
    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());

    // Well above threshold
    fsm.on_ctl(200, 0.5f, 0.0f, 2);
    armed = fsm.on_arm(220, true);
    TEST_ASSERT_FALSE(armed);
    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());
}

void test_fsm_arm_allowed_with_thr_lt_005(void) {
    FlightFsm fsm;

    fsm.on_ctl(100, 0.049f, 0.0f, 1);
    bool armed = fsm.on_arm(120, true);
    TEST_ASSERT_TRUE(armed);
    TEST_ASSERT_EQUAL(FlightState::ARMED, fsm.state());
    TEST_ASSERT_EQUAL_STRING("ARMED", fsm.state_str());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.049f, fsm.effective_thr());
}

void test_fsm_link_timeout_triggers_failsafe(void) {
    FlightFsm fsm;

    // Arm cleanly
    fsm.on_ctl(100, 0.0f, 0.0f, 1);
    TEST_ASSERT_TRUE(fsm.on_arm(120, true));
    TEST_ASSERT_EQUAL(FlightState::ARMED, fsm.state());

    // Update with flying throttle
    fsm.on_ctl(200, 0.75f, 0.0f, 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, fsm.effective_thr());

    // Step at age = 500 ms (t = 700 ms) -> still ARMED
    fsm.step(700);
    TEST_ASSERT_EQUAL(FlightState::ARMED, fsm.state());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, fsm.effective_thr());

    // Step at age = 501 ms (t = 701 ms) -> FAILSAFE!
    fsm.step(701);
    TEST_ASSERT_EQUAL(FlightState::FAILSAFE, fsm.state());
    TEST_ASSERT_EQUAL_STRING("FAILSAFE", fsm.state_str());

    // In FAILSAFE, effective throttle MUST be 0.0f
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, fsm.effective_thr());
    // Raw last throttle preserved for diagnostics
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, fsm.last_throttle());
}

void test_fsm_link_closed_triggers_immediate_failsafe(void) {
    FlightFsm fsm;

    // Link close while DISARMED remains DISARMED
    fsm.on_link_closed();
    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());

    // Arm
    fsm.on_ctl(100, 0.0f, 0.0f, 1);
    TEST_ASSERT_TRUE(fsm.on_arm(120, true));
    TEST_ASSERT_EQUAL(FlightState::ARMED, fsm.state());

    // WebSocket closes/superseded -> immediate FAILSAFE
    fsm.on_link_closed();
    TEST_ASSERT_EQUAL(FlightState::FAILSAFE, fsm.state());
    TEST_ASSERT_FALSE(fsm.has_ctl());
    TEST_ASSERT_EQUAL_UINT32(9999u, fsm.age_ms(200));
}

void test_fsm_arm_true_ignored_in_failsafe(void) {
    FlightFsm fsm;

    // Put into FAILSAFE
    fsm.on_ctl(100, 0.0f, 0.0f, 1);
    fsm.on_arm(120, true);
    fsm.on_link_closed();
    TEST_ASSERT_EQUAL(FlightState::FAILSAFE, fsm.state());

    // New link comes up with valid zero throttle
    fsm.on_ctl(500, 0.0f, 0.0f, 2);
    TEST_ASSERT_TRUE(fsm.has_ctl());

    // arm {v: true} must be IGNORED in FAILSAFE
    bool armed = fsm.on_arm(520, true);
    TEST_ASSERT_FALSE(armed);
    TEST_ASSERT_EQUAL(FlightState::FAILSAFE, fsm.state());
}

void test_fsm_arm_false_restores_disarmed_and_rearm_succeeds(void) {
    FlightFsm fsm;

    // Reach FAILSAFE
    fsm.on_ctl(100, 0.0f, 0.0f, 1);
    fsm.on_arm(120, true);
    fsm.step(701);
    TEST_ASSERT_EQUAL(FlightState::FAILSAFE, fsm.state());

    // Explicit disarm: arm {v: false} transitions FAILSAFE -> DISARMED
    bool disarmed = fsm.on_arm(800, false);
    TEST_ASSERT_TRUE(disarmed);
    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());

    // Re-arm cleanly
    fsm.on_ctl(850, 0.0f, 0.0f, 2);
    bool rearmed = fsm.on_arm(870, true);
    TEST_ASSERT_TRUE(rearmed);
    TEST_ASSERT_EQUAL(FlightState::ARMED, fsm.state());

    // Disarm from ARMED
    bool disarmed_again = fsm.on_arm(900, false);
    TEST_ASSERT_TRUE(disarmed_again);
    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());

    // Redundant disarm when already DISARMED is a no-op
    bool noop = fsm.on_arm(920, false);
    TEST_ASSERT_FALSE(noop);
    TEST_ASSERT_EQUAL(FlightState::DISARMED, fsm.state());
}

void test_fsm_lowbatt_latch_and_refusal(void) {
    FlightFsm fsm;

    // Transition directly to LOWBATT from battery cutoff
    fsm.on_battery_cutoff();
    TEST_ASSERT_EQUAL(FlightState::LOWBATT, fsm.state());
    TEST_ASSERT_EQUAL_STRING("LOWBATT", fsm.state_str());

    // Arming refused when in LOWBATT
    fsm.on_ctl(100, 0.0f, 0.0f, 1);
    ArmResult res = fsm.on_arm_result(120, true);
    TEST_ASSERT_FALSE(res.success);
    TEST_ASSERT_EQUAL(ArmRefusal::BATT, res.reason);

    // Disarming attempt in LOWBATT cannot clear latched LOWBATT
    ArmResult res_disarm = fsm.on_arm_result(130, false);
    TEST_ASSERT_FALSE(res_disarm.success);
    TEST_ASSERT_EQUAL(FlightState::LOWBATT, fsm.state());

    // Clean FSM refused arming when battery_ok == false
    FlightFsm clean_fsm;
    clean_fsm.on_ctl(100, 0.0f, 0.0f, 1);
    ArmResult res_bad_batt = clean_fsm.on_arm_result(120, true, false /* battery_ok */);
    TEST_ASSERT_FALSE(res_bad_batt.success);
    TEST_ASSERT_EQUAL(ArmRefusal::BATT, res_bad_batt.reason);
    TEST_ASSERT_EQUAL(FlightState::DISARMED, clean_fsm.state());
}

void test_fsm_stop_and_release_lock(void) {
    FlightFsm fsm;

    // Arm drone
    fsm.on_ctl(100, 0.0f, 0.0f, 1);
    TEST_ASSERT_TRUE(fsm.on_arm(120, true));
    TEST_ASSERT_EQUAL(FlightState::ARMED, fsm.state());

    // Pilot flying with throttle 0.8 at t=200ms
    fsm.on_ctl(200, 0.8f, 0.0f, 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8f, fsm.effective_thr());
    TEST_ASSERT_EQUAL_UINT32(200u, fsm.last_ctl_ms());

    // Pilot releases stick at t=250ms -> fires on_stop(250, 3)
    TEST_ASSERT_TRUE(fsm.on_stop(250, 3));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, fsm.effective_thr());
    TEST_ASSERT_TRUE(fsm.is_release_locked(250));
    // CRUCIAL: on_stop does NOT update last_ctl_ms_
    TEST_ASSERT_EQUAL_UINT32(200u, fsm.last_ctl_ms());

    // At t=350ms (in 250ms lock until 500ms): ctl with thr=0.5 -> ignored (clamped to 0)
    fsm.on_ctl(350, 0.5f, 0.0f, 4);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, fsm.effective_thr());
    TEST_ASSERT_EQUAL_UINT32(350u, fsm.last_ctl_ms());

    // At t=501ms (lock expired): ctl with thr=0.5 -> accepted!
    fsm.on_ctl(501, 0.5f, 0.0f, 5);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, fsm.effective_thr());
}

void test_fsm_arm_refusal_reasons(void) {
    FlightFsm fsm;

    // 1. Refusal: NO_LINK (no ctl received)
    ArmResult r1 = fsm.on_arm_result(100, true);
    TEST_ASSERT_EQUAL(ArmRefusal::NO_LINK, r1.reason);
    TEST_ASSERT_EQUAL_STRING("link", FlightFsm::to_refusal_str(r1.reason));

    // 2. Refusal: THROTTLE (thr >= 0.05f)
    fsm.on_ctl(100, 0.2f, 0.0f, 1);
    ArmResult r2 = fsm.on_arm_result(120, true);
    TEST_ASSERT_EQUAL(ArmRefusal::THROTTLE, r2.reason);
    TEST_ASSERT_EQUAL_STRING("thr", FlightFsm::to_refusal_str(r2.reason));

    // 3. Refusal: NO_LINK (stale ctl > 500ms)
    fsm.on_ctl(100, 0.0f, 0.0f, 2);
    ArmResult r3 = fsm.on_arm_result(650, true);
    TEST_ASSERT_EQUAL(ArmRefusal::NO_LINK, r3.reason);

    // 4. Refusal: INVALID_STATE (already armed)
    fsm.on_ctl(700, 0.0f, 0.0f, 3);
    ArmResult r_ok = fsm.on_arm_result(720, true);
    TEST_ASSERT_TRUE(r_ok.success);
    TEST_ASSERT_EQUAL(ArmRefusal::NONE, r_ok.reason);
    ArmResult r4 = fsm.on_arm_result(730, true);
    TEST_ASSERT_EQUAL(ArmRefusal::INVALID_STATE, r4.reason);
    TEST_ASSERT_EQUAL_STRING("state", FlightFsm::to_refusal_str(r4.reason));
}

// ============================================================================
// 3. ProtocolCodec Tests
// ============================================================================

void test_protocol_codec_parse_all_7_examples(void) {
    // 1. arm_false.json: {"t":"arm","v":false}
    const char* arm_false_json = "{\"t\":\"arm\",\"v\":false}";
    TEST_ASSERT_EQUAL(FrameType::ARM, identify_frame(arm_false_json));
    ArmFrame arm_f;
    TEST_ASSERT_TRUE(parse_arm(arm_false_json, arm_f));
    TEST_ASSERT_FALSE(arm_f.v);

    // 2. arm_true.json: {"t":"arm","v":true}
    const char* arm_true_json = "{\"t\":\"arm\",\"v\":true}";
    TEST_ASSERT_EQUAL(FrameType::ARM, identify_frame(arm_true_json));
    ArmFrame arm_t;
    TEST_ASSERT_TRUE(parse_arm(arm_true_json, arm_t));
    TEST_ASSERT_TRUE(arm_t.v);

    // 3. ctl.json: {"t":"ctl","seq":1001,"thr":0.525,"str":0.0}
    const char* ctl_json = "{\"t\":\"ctl\",\"seq\":1001,\"thr\":0.525,\"str\":0.0}";
    TEST_ASSERT_EQUAL(FrameType::CTL, identify_frame(ctl_json));
    CtlFrame ctl;
    TEST_ASSERT_TRUE(parse_ctl(ctl_json, ctl));
    TEST_ASSERT_EQUAL_UINT32(1001u, ctl.seq);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.525f, ctl.thr);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, ctl.str);

    // 4. hello.json: {"t":"hello","proto":1,"fw":"0.1.0"}
    const char* hello_json = "{\"t\":\"hello\",\"proto\":1,\"fw\":\"0.1.0\"}";
    TEST_ASSERT_EQUAL(FrameType::HELLO, identify_frame(hello_json));
    HelloFrame hello;
    TEST_ASSERT_TRUE(parse_hello(hello_json, hello));
    TEST_ASSERT_EQUAL_INT(1, hello.proto);
    TEST_ASSERT_EQUAL_STRING("0.1.0", hello.fw);

    // 5. tel_armed.json: {"t":"tel","st":"ARMED","f":2.5,"thr":0.525,"age":24}
    const char* tel_armed_json = "{\"t\":\"tel\",\"st\":\"ARMED\",\"f\":2.5,\"thr\":0.525,\"age\":24}";
    TEST_ASSERT_EQUAL(FrameType::TEL, identify_frame(tel_armed_json));
    TelFrame tel_a;
    TEST_ASSERT_TRUE(parse_tel(tel_armed_json, tel_a));
    TEST_ASSERT_EQUAL_STRING("ARMED", tel_a.st);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.5f, tel_a.f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.525f, tel_a.thr);
    TEST_ASSERT_EQUAL_UINT32(24u, tel_a.age);

    // 6. tel_disarmed.json: {"t":"tel","st":"DISARMED","f":0.0,"thr":0.0,"age":9999}
    const char* tel_disarmed_json = "{\"t\":\"tel\",\"st\":\"DISARMED\",\"f\":0.0,\"thr\":0.0,\"age\":9999}";
    TEST_ASSERT_EQUAL(FrameType::TEL, identify_frame(tel_disarmed_json));
    TelFrame tel_d;
    TEST_ASSERT_TRUE(parse_tel(tel_disarmed_json, tel_d));
    TEST_ASSERT_EQUAL_STRING("DISARMED", tel_d.st);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, tel_d.f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, tel_d.thr);
    TEST_ASSERT_EQUAL_UINT32(9999u, tel_d.age);

    // 7. tel_failsafe.json: {"t":"tel","st":"FAILSAFE","f":0.0,"thr":0.0,"age":512}
    const char* tel_failsafe_json = "{\"t\":\"tel\",\"st\":\"FAILSAFE\",\"f\":0.0,\"thr\":0.0,\"age\":512}";
    TEST_ASSERT_EQUAL(FrameType::TEL, identify_frame(tel_failsafe_json));
    TelFrame tel_fs;
    TEST_ASSERT_TRUE(parse_tel(tel_failsafe_json, tel_fs));
    TEST_ASSERT_EQUAL_STRING("FAILSAFE", tel_fs.st);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, tel_fs.f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, tel_fs.thr);
    TEST_ASSERT_EQUAL_UINT32(512u, tel_fs.age);

    // 8. ctl_steer.json: {"t":"ctl","seq":1002,"thr":0.525,"str":-0.3}
    const char* ctl_steer_json = "{\"t\":\"ctl\",\"seq\":1002,\"thr\":0.525,\"str\":-0.3}";
    TEST_ASSERT_EQUAL(FrameType::CTL, identify_frame(ctl_steer_json));
    CtlFrame ctl_s;
    TEST_ASSERT_TRUE(parse_ctl(ctl_steer_json, ctl_s));
    TEST_ASSERT_EQUAL_UINT32(1002u, ctl_s.seq);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.525f, ctl_s.thr);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.3f, ctl_s.str);

    // 9. stop.json: {"t":"stop","seq":1730208042}
    const char* stop_json = "{\"t\":\"stop\",\"seq\":1730208042}";
    TEST_ASSERT_EQUAL(FrameType::STOP, identify_frame(stop_json));
    StopFrame stop_f;
    TEST_ASSERT_TRUE(parse_stop(stop_json, stop_f));
    TEST_ASSERT_EQUAL_UINT32(1730208042u, stop_f.seq);

    // 10. tel_armed_v2.json: {"t":"tel","st":"ARMED","f":2.5,"thr":0.525,"str":-0.3,"vb":7.82,"vpct":71,"age":24}
    const char* tel_v2_json = "{\"t\":\"tel\",\"st\":\"ARMED\",\"f\":2.5,\"thr\":0.525,\"str\":-0.3,\"vb\":7.82,\"vpct\":71,\"age\":24}";
    TEST_ASSERT_EQUAL(FrameType::TEL, identify_frame(tel_v2_json));
    TelFrame tel_v2;
    TEST_ASSERT_TRUE(parse_tel(tel_v2_json, tel_v2));
    TEST_ASSERT_EQUAL_STRING("ARMED", tel_v2.st);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.5f, tel_v2.f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.525f, tel_v2.thr);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.3f, tel_v2.str);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 7.82f, tel_v2.vb);
    TEST_ASSERT_EQUAL_INT(71, tel_v2.vpct);
    TEST_ASSERT_EQUAL_UINT32(24u, tel_v2.age);

    // 11. tel_lowbatt.json: {"t":"tel","st":"LOWBATT","f":0.0,"thr":0.0,"str":0.0,"vb":6.35,"vpct":0,"age":24}
    const char* tel_lowbatt_json = "{\"t\":\"tel\",\"st\":\"LOWBATT\",\"f\":0.0,\"thr\":0.0,\"str\":0.0,\"vb\":6.35,\"vpct\":0,\"age\":24}";
    TEST_ASSERT_EQUAL(FrameType::TEL, identify_frame(tel_lowbatt_json));
    TelFrame tel_lb;
    TEST_ASSERT_TRUE(parse_tel(tel_lowbatt_json, tel_lb));
    TEST_ASSERT_EQUAL_STRING("LOWBATT", tel_lb.st);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 6.35f, tel_lb.vb);
    TEST_ASSERT_EQUAL_INT(0, tel_lb.vpct);

    // 12. err_arm_refused.json: {"t":"err","code":"arm_refused","msg":"batt"}
    const char* err_arm_json = "{\"t\":\"err\",\"code\":\"arm_refused\",\"msg\":\"batt\"}";
    TEST_ASSERT_EQUAL(FrameType::ERR, identify_frame(err_arm_json));
    ErrFrame err_f;
    TEST_ASSERT_TRUE(parse_err(err_arm_json, err_f));
    TEST_ASSERT_EQUAL_STRING("arm_refused", err_f.code);
    TEST_ASSERT_EQUAL_STRING("batt", err_f.msg);

    // 13. err_failsafe_link.json: {"t":"err","code":"failsafe_link","msg":"timeout"}
    const char* err_fs_json = "{\"t\":\"err\",\"code\":\"failsafe_link\",\"msg\":\"timeout\"}";
    TEST_ASSERT_EQUAL(FrameType::ERR, identify_frame(err_fs_json));
    TEST_ASSERT_TRUE(parse_err(err_fs_json, err_f));
    TEST_ASSERT_EQUAL_STRING("failsafe_link", err_f.code);
    TEST_ASSERT_EQUAL_STRING("timeout", err_f.msg);

    // 14. err_lowbatt.json: {"t":"err","code":"lowbatt","msg":"voltage_cutoff"}
    const char* err_lb_json = "{\"t\":\"err\",\"code\":\"lowbatt\",\"msg\":\"voltage_cutoff\"}";
    TEST_ASSERT_EQUAL(FrameType::ERR, identify_frame(err_lb_json));
    TEST_ASSERT_TRUE(parse_err(err_lb_json, err_f));
    TEST_ASSERT_EQUAL_STRING("lowbatt", err_f.code);
    TEST_ASSERT_EQUAL_STRING("voltage_cutoff", err_f.msg);
}

void test_protocol_codec_malformed_json(void) {
    CtlFrame ctl;
    ArmFrame arm;

    TEST_ASSERT_FALSE(parse_ctl(nullptr, ctl));
    TEST_ASSERT_FALSE(parse_ctl("", ctl));
    TEST_ASSERT_FALSE(parse_ctl("{malformed", ctl));
    TEST_ASSERT_FALSE(parse_ctl("not json at all", ctl));
    TEST_ASSERT_FALSE(parse_ctl("[1,2,3]", ctl));

    TEST_ASSERT_FALSE(parse_arm(nullptr, arm));
    TEST_ASSERT_FALSE(parse_arm("", arm));
    TEST_ASSERT_FALSE(parse_arm("{\"t\":\"arm\",", arm));

    TEST_ASSERT_EQUAL(FrameType::UNKNOWN, identify_frame(nullptr));
    TEST_ASSERT_EQUAL(FrameType::UNKNOWN, identify_frame(""));
    TEST_ASSERT_EQUAL(FrameType::UNKNOWN, identify_frame("{bad"));
}

void test_protocol_codec_invalid_types(void) {
    CtlFrame ctl;
    ArmFrame arm;

    // Wrong t
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"arm\",\"seq\":1,\"thr\":0.5}", ctl));
    TEST_ASSERT_FALSE(parse_arm("{\"t\":\"ctl\",\"v\":true}", arm));

    // Missing seq
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"ctl\",\"thr\":0.5}", ctl));
    // Negative seq
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"ctl\",\"seq\":-1,\"thr\":0.5}", ctl));
    // Float seq
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"ctl\",\"seq\":1.5,\"thr\":0.5}", ctl));
    // Bool seq
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"ctl\",\"seq\":true,\"thr\":0.5}", ctl));
    // String seq
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"ctl\",\"seq\":\"100\",\"thr\":0.5}", ctl));

    // Missing thr
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"ctl\",\"seq\":1}", ctl));
    // String thr
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"ctl\",\"seq\":1,\"thr\":\"0.5\"}", ctl));
    // Bool thr
    TEST_ASSERT_FALSE(parse_ctl("{\"t\":\"ctl\",\"seq\":1,\"thr\":false}", ctl));

    // Missing v
    TEST_ASSERT_FALSE(parse_arm("{\"t\":\"arm\"}", arm));
    // Integer v
    TEST_ASSERT_FALSE(parse_arm("{\"t\":\"arm\",\"v\":1}", arm));
    // String v
    TEST_ASSERT_FALSE(parse_arm("{\"t\":\"arm\",\"v\":\"true\"}", arm));
}

void test_protocol_codec_extra_fields_and_clamping(void) {
    CtlFrame ctl;

    // Over-clamp thr and str, optional str omitted
    const char* ctl_no_str = "{\"t\":\"ctl\",\"seq\":200,\"thr\":1.8,\"unknown\":999}";
    TEST_ASSERT_TRUE(parse_ctl(ctl_no_str, ctl));
    TEST_ASSERT_EQUAL_UINT32(200u, ctl.seq);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, ctl.thr);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ctl.str);  // default 0.0f

    // Under-clamp thr and str
    const char* ctl_under = "{\"t\":\"ctl\",\"seq\":201,\"thr\":-0.5,\"str\":-3.0,\"foo\":\"bar\"}";
    TEST_ASSERT_TRUE(parse_ctl(ctl_under, ctl));
    TEST_ASSERT_EQUAL_UINT32(201u, ctl.seq);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, ctl.thr);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, ctl.str);

    // Extra fields in arm
    ArmFrame arm;
    const char* arm_extra = "{\"t\":\"arm\",\"v\":true,\"timestamp\":123456789}";
    TEST_ASSERT_TRUE(parse_arm(arm_extra, arm));
    TEST_ASSERT_TRUE(arm.v);
}

void test_protocol_codec_serialization(void) {
    char buf[160];

    // serialize_hello (proto 2, fw 0.2.0)
    size_t len_hello = serialize_hello(buf, sizeof(buf), "0.2.0");
    TEST_ASSERT_TRUE(len_hello > 0);
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"hello\",\"proto\":2,\"fw\":\"0.2.0\"}", buf);

    // serialize_tel v2
    size_t len_tel = serialize_tel(buf, sizeof(buf), "ARMED", 2.5f, 0.525f, -0.3f, 7.82f, 71, 24);
    TEST_ASSERT_TRUE(len_tel > 0);

    // Check round-trip through parse_tel
    TelFrame parsed_tel;
    TEST_ASSERT_TRUE(parse_tel(buf, len_tel, parsed_tel));
    TEST_ASSERT_EQUAL_STRING("ARMED", parsed_tel.st);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.5f, parsed_tel.f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.525f, parsed_tel.thr);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.3f, parsed_tel.str);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 7.82f, parsed_tel.vb);
    TEST_ASSERT_EQUAL_INT(71, parsed_tel.vpct);
    TEST_ASSERT_EQUAL_UINT32(24u, parsed_tel.age);

    // serialize_err
    size_t len_err = serialize_err(buf, sizeof(buf), "arm_refused", "batt");
    TEST_ASSERT_TRUE(len_err > 0);
    ErrFrame parsed_err;
    TEST_ASSERT_TRUE(parse_err(buf, len_err, parsed_err));
    TEST_ASSERT_EQUAL_STRING("arm_refused", parsed_err.code);
    TEST_ASSERT_EQUAL_STRING("batt", parsed_err.msg);

    // Null buffer / zero size safety
    TEST_ASSERT_EQUAL_UINT32(0u, serialize_hello(nullptr, 0));
    TEST_ASSERT_EQUAL_UINT32(0u, serialize_tel(nullptr, 0, "ARMED", 0.0f, 0.0f, 0));
    TEST_ASSERT_EQUAL_UINT32(0u, serialize_err(nullptr, 0, "code", "msg"));
}

// ============================================================================
// 4. WsControlSource Tests
// ============================================================================

void test_ws_control_source_sequence_handling(void) {
    WsControlSource ws;

    TEST_ASSERT_FALSE(ws.has_seq());

    // 1. First seq of session is always accepted
    TEST_ASSERT_TRUE(ws.accept_ctl(1000, 0.5f, 0.0f, 100));
    TEST_ASSERT_TRUE(ws.has_seq());
    TEST_ASSERT_EQUAL_UINT32(1000u, ws.last_seq());

    // 2. Incremental sequence accepted
    TEST_ASSERT_TRUE(ws.accept_ctl(1001, 0.5f, 0.0f, 140));
    TEST_ASSERT_EQUAL_UINT32(1001u, ws.last_seq());

    // Jump forward accepted
    TEST_ASSERT_TRUE(ws.accept_ctl(1050, 0.5f, 0.0f, 180));
    TEST_ASSERT_EQUAL_UINT32(1050u, ws.last_seq());

    // 3. Duplicate sequence rejected
    TEST_ASSERT_FALSE(ws.accept_ctl(1050, 0.5f, 0.0f, 220));
    TEST_ASSERT_EQUAL_UINT32(1050u, ws.last_seq());

    // 4. Stale sequence rejected
    TEST_ASSERT_FALSE(ws.accept_ctl(1049, 0.5f, 0.0f, 260));
    TEST_ASSERT_FALSE(ws.accept_ctl(1000, 0.5f, 0.0f, 300));
    TEST_ASSERT_EQUAL_UINT32(1050u, ws.last_seq());

    // 5. 32-bit wrap around (e.g. 0xFFFFFFFF -> 0x00000005) accepted
    ws.reset();
    TEST_ASSERT_TRUE(ws.accept_ctl(0xFFFFFFFFu, 0.5f, 0.0f, 340));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, ws.last_seq());

    // Wrap around to 5: (int32_t)(5 - 0xFFFFFFFF) = 6 > 0 -> ACCEPTED
    TEST_ASSERT_TRUE(ws.accept_ctl(0x00000005u, 0.5f, 0.0f, 380));
    TEST_ASSERT_EQUAL_UINT32(5u, ws.last_seq());

    // Old sequence now rejected
    TEST_ASSERT_FALSE(ws.accept_ctl(0xFFFFFFFFu, 0.5f, 0.0f, 420));
    TEST_ASSERT_EQUAL_UINT32(5u, ws.last_seq());

    // Reset clears state
    ws.reset();
    TEST_ASSERT_FALSE(ws.has_seq());
    // New first seq accepted
    TEST_ASSERT_TRUE(ws.accept_ctl(50, 0.0f, 0.0f, 500));
    TEST_ASSERT_EQUAL_UINT32(50u, ws.last_seq());
}

// ============================================================================
// 5. ServoEase Tests
// ============================================================================

void test_servo_ease_lifecycle_and_pwm_flag(void) {
    ServoEase ease(300.0f, 0.3f, 90.0f);

    float left = 0.0f;
    float right = 0.0f;
    bool pwm_active = true;

    // Boot / DISARMED -> IDLE_LOW, PWM inactive
    ease.update(0.05f, FlightState::DISARMED, 90.0f, 90.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::IDLE_LOW, ease.state());
    TEST_ASSERT_FALSE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, right);

    // Arm drone -> transitions to ARMING_RAMP, PWM active
    ease.update(0.10f, FlightState::ARMED, 120.0f, 60.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::ARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);

    // Slew limit check: 300 deg/s * 0.10 s = 30 deg max travel
    // From 90 -> 120 is 30 deg, exactly reached
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 120.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.0f, right);

    // Step 0.10 s (total ramp 0.20 s < 0.30 s) -> still ARMING_RAMP
    ease.update(0.10f, FlightState::ARMED, 120.0f, 60.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::ARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);

    // Step 0.10 s (total ramp 0.30 s >= 0.30 s) -> transitions to ACTIVE_PWM
    ease.update(0.10f, FlightState::ARMED, 120.0f, 60.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::ACTIVE_PWM, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 120.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.0f, right);

    // In ACTIVE_PWM, follows targets immediately
    ease.update(0.02f, FlightState::ARMED, 165.0f, 15.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::ACTIVE_PWM, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 165.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 15.0f, right);

    // Disarm / Failsafe triggers DISARMING_RAMP; PWM remains active while ramping!
    ease.update(0.10f, FlightState::FAILSAFE, 165.0f, 15.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::DISARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    // Slewing back towards 90: delta = 300 * 0.1 = 30 deg. left was 165 -> 135, right was 15 -> 45
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 135.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 45.0f, right);

    // Step 0.10 s (elapsed 0.20 s)
    ease.update(0.10f, FlightState::FAILSAFE, 165.0f, 15.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::DISARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 105.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 75.0f, right);

    // Step 0.10 s (elapsed 0.30 s >= 0.30 s) -> completes ramp, transitions to IDLE_LOW, PWM turns off!
    ease.update(0.10f, FlightState::FAILSAFE, 165.0f, 15.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::IDLE_LOW, ease.state());
    TEST_ASSERT_FALSE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, right);

    // Stays IDLE_LOW and PWM false
    ease.update(0.05f, FlightState::DISARMED, 90.0f, 90.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::IDLE_LOW, ease.state());
    TEST_ASSERT_FALSE(pwm_active);
}

void test_servo_ease_micro_write_filter(void) {
    // Default threshold = 0.12 deg
    TEST_ASSERT_FALSE(should_write_servo(90.00f, 90.05f));
    TEST_ASSERT_FALSE(should_write_servo(90.00f, 90.119f));
    TEST_ASSERT_TRUE(should_write_servo(90.00f, 90.120f));
    TEST_ASSERT_TRUE(should_write_servo(90.00f, 90.25f));
    TEST_ASSERT_TRUE(should_write_servo(90.00f, 89.85f));

    // Custom threshold
    TEST_ASSERT_FALSE(should_write_servo(90.0f, 90.4f, 0.50f));
    TEST_ASSERT_TRUE(should_write_servo(90.0f, 90.6f, 0.50f));
}

void test_servo_ease_disarm_ramp_safety_clamp(void) {
    // Slew rate = 100 deg/s, ramp_duration = 0.20 s, center = 90 deg.
    // In ramp_duration (0.20 s), at 100 deg/s max travel is only 20 deg.
    ServoEase ease(100.0f, 0.20f, 90.0f);

    float left = 0.0f;
    float right = 0.0f;
    bool pwm_active = false;

    // Start in IDLE_LOW
    ease.update(0.01f, FlightState::DISARMED, 90.0f, 90.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::IDLE_LOW, ease.state());
    TEST_ASSERT_FALSE(pwm_active);

    // Arm and advance to ACTIVE_PWM: target 150 deg left, 30 deg right (60 deg from center).
    // Ramp duration = 0.20 s. Step 0.20 s in ARMED.
    ease.update(0.20f, FlightState::ARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::ACTIVE_PWM, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 150.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 30.0f, right);

    // Disarm trigger: enters DISARMING_RAMP
    // Step 1: dt = 0.10 s. max_delta = 100 * 0.10 = 10 deg.
    // Left: 150 -> 140, Right: 30 -> 40. ramp_elapsed = 0.10 s < 0.20 s.
    ease.update(0.10f, FlightState::DISARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::DISARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 140.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 40.0f, right);

    // Step 2: dt = 0.10 s. ramp_elapsed = 0.20 s >= ramp_duration (0.20 s).
    // max_delta = 10 deg. Left: 140 -> 130, Right: 40 -> 50.
    // CRITICAL SAFETY CHECK:
    // ramp_elapsed >= ramp_duration, but angles (130, 50) have NOT reached center (90).
    // Must remain in DISARMING_RAMP and out_pwm_active MUST remain true!
    ease.update(0.10f, FlightState::DISARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::DISARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 130.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, right);

    // Step 3: dt = 0.10 s (elapsed 0.30 s). Left -> 120, Right -> 60. Still DISARMING_RAMP, PWM active.
    ease.update(0.10f, FlightState::DISARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::DISARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 120.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.0f, right);

    // Step 4: dt = 0.10 s (elapsed 0.40 s). Left -> 110, Right -> 70. Still DISARMING_RAMP, PWM active.
    ease.update(0.10f, FlightState::DISARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::DISARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 110.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 70.0f, right);

    // Step 5: dt = 0.10 s (elapsed 0.50 s). Left -> 100, Right -> 80. Still DISARMING_RAMP, PWM active.
    ease.update(0.10f, FlightState::DISARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::DISARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 80.0f, right);

    // Step 6: dt = 0.10 s (elapsed 0.60 s). Left -> 90, Right -> 90.
    // Both angles have now reached center (<= 0.01 deg delta) AND ramp_elapsed >= ramp_duration.
    // Now transitions to IDLE_LOW and out_pwm_active turns false!
    ease.update(0.10f, FlightState::DISARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::IDLE_LOW, ease.state());
    TEST_ASSERT_FALSE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, right);
}

void test_servo_ease_disarm_from_arming_ramp_safety_clamp(void) {
    // Slew rate = 100 deg/s, ramp_duration = 0.20 s, center = 90 deg.
    ServoEase ease(100.0f, 0.20f, 90.0f);

    float left = 0.0f;
    float right = 0.0f;
    bool pwm_active = false;

    // Step into ARMING_RAMP towards 150 / 30 for 0.10 s -> left = 100, right = 80
    ease.update(0.10f, FlightState::ARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::ARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 80.0f, right);

    // Disarm directly from ARMING_RAMP: transitions to DISARMING_RAMP
    // Step 0.05 s: delta = 5 deg. Left: 100 -> 95, Right: 80 -> 85.
    ease.update(0.05f, FlightState::DISARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::DISARMING_RAMP, ease.state());
    TEST_ASSERT_TRUE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 95.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 85.0f, right);

    // Step 0.16 s: total ramp_elapsed = 0.21 s >= 0.20 s. max_delta = 16 deg.
    // Left reaches 90.0, Right reaches 90.0.
    // Angles reach center AND ramp_elapsed >= ramp_duration -> IDLE_LOW, PWM false.
    ease.update(0.16f, FlightState::DISARMED, 150.0f, 30.0f, left, right, pwm_active);
    TEST_ASSERT_EQUAL(ServoEaseState::IDLE_LOW, ease.state());
    TEST_ASSERT_FALSE(pwm_active);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, left);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, right);
}

// ============================================================================
// Unity Test Runner
// ============================================================================

int run_unity_tests(void) {
    UNITY_BEGIN();

    // FlapEngine
    RUN_TEST(test_flap_engine_defaults_and_deadband);
    RUN_TEST(test_flap_engine_frequency_mapping);
    RUN_TEST(test_flap_engine_triangle_wave_and_amplitude);
    RUN_TEST(test_flap_engine_phase_reset);
    RUN_TEST(test_flap_engine_runtime_param_changes);
    RUN_TEST(test_flap_engine_differential_mixer_and_idle_lean);

    // BatteryMonitor
    RUN_TEST(test_battery_monitor_divider_and_percentage);
    RUN_TEST(test_battery_monitor_ema_filter_and_cutoff_latch);

    // FlightFSM
    RUN_TEST(test_fsm_boot_state);
    RUN_TEST(test_fsm_arm_denied_without_ctl);
    RUN_TEST(test_fsm_arm_denied_with_stale_ctl_age_gt_500);
    RUN_TEST(test_fsm_arm_denied_with_thr_gte_005);
    RUN_TEST(test_fsm_arm_allowed_with_thr_lt_005);
    RUN_TEST(test_fsm_link_timeout_triggers_failsafe);
    RUN_TEST(test_fsm_link_closed_triggers_immediate_failsafe);
    RUN_TEST(test_fsm_arm_true_ignored_in_failsafe);
    RUN_TEST(test_fsm_arm_false_restores_disarmed_and_rearm_succeeds);
    RUN_TEST(test_fsm_lowbatt_latch_and_refusal);
    RUN_TEST(test_fsm_stop_and_release_lock);
    RUN_TEST(test_fsm_arm_refusal_reasons);

    // ProtocolCodec
    RUN_TEST(test_protocol_codec_parse_all_7_examples);
    RUN_TEST(test_protocol_codec_malformed_json);
    RUN_TEST(test_protocol_codec_invalid_types);
    RUN_TEST(test_protocol_codec_extra_fields_and_clamping);
    RUN_TEST(test_protocol_codec_serialization);

    // WsControlSource
    RUN_TEST(test_ws_control_source_sequence_handling);

    // ServoEase
    RUN_TEST(test_servo_ease_lifecycle_and_pwm_flag);
    RUN_TEST(test_servo_ease_micro_write_filter);
    RUN_TEST(test_servo_ease_disarm_ramp_safety_clamp);
    RUN_TEST(test_servo_ease_disarm_from_arming_ramp_safety_clamp);

    return UNITY_END();
}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    run_unity_tests();
}
void loop() {}
#else
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return run_unity_tests();
}
#endif
