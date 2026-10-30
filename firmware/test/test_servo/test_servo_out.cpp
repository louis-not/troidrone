#include <unity.h>
#include <cmath>

#include "pins.h"
#include "servo_out.h"
#include "servo_out.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_servo_out_begin_and_initial_state(void) {
    ServoOut servo;
    servo.begin();

    TEST_ASSERT_FALSE(servo.is_pwm_active());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_right_deg());
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_LEFT, servo.left_pin());
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_RIGHT, servo.right_pin());
}

void test_servo_out_angle_to_pulse_us(void) {
    // Nominal mapping: 0.0° -> 500 µs, 90.0° -> 1450 µs, 180.0° -> 2400 µs
    TEST_ASSERT_EQUAL_UINT32(500, ServoOut::angle_to_pulse_us(0.0f));
    TEST_ASSERT_EQUAL_UINT32(1450, ServoOut::angle_to_pulse_us(90.0f));
    TEST_ASSERT_EQUAL_UINT32(2400, ServoOut::angle_to_pulse_us(180.0f));

    // Intermediate linear check: 45° -> 500 + 0.25 * 1900 = 975 µs
    TEST_ASSERT_EQUAL_UINT32(975, ServoOut::angle_to_pulse_us(45.0f));
    // 135° -> 500 + 0.75 * 1900 = 1925 µs
    TEST_ASSERT_EQUAL_UINT32(1925, ServoOut::angle_to_pulse_us(135.0f));

    // Clamping: < 0.0° clamps to 500 µs
    TEST_ASSERT_EQUAL_UINT32(500, ServoOut::angle_to_pulse_us(-0.01f));
    TEST_ASSERT_EQUAL_UINT32(500, ServoOut::angle_to_pulse_us(-10.0f));
    TEST_ASSERT_EQUAL_UINT32(500, ServoOut::angle_to_pulse_us(-999.0f));

    // Clamping: > 180.0° clamps to 2400 µs
    TEST_ASSERT_EQUAL_UINT32(2400, ServoOut::angle_to_pulse_us(180.01f));
    TEST_ASSERT_EQUAL_UINT32(2400, ServoOut::angle_to_pulse_us(250.0f));
    TEST_ASSERT_EQUAL_UINT32(2400, ServoOut::angle_to_pulse_us(999.0f));
}

void test_servo_out_pulse_us_to_duty(void) {
    // 14-bit resolution over 20,000 µs (50 Hz): duty = (pulse_us * 16384) / 20000
    // 500 µs:  (500 * 16384) / 20000  = 409
    TEST_ASSERT_EQUAL_UINT32(409, ServoOut::pulse_us_to_duty(500));

    // 1450 µs: (1450 * 16384) / 20000 = 1187
    TEST_ASSERT_EQUAL_UINT32(1187, ServoOut::pulse_us_to_duty(1450));

    // 2400 µs: (2400 * 16384) / 20000 = 1966
    TEST_ASSERT_EQUAL_UINT32(1966, ServoOut::pulse_us_to_duty(2400));
}

void test_servo_out_angle_to_duty(void) {
    // End-to-end conversion from angle to 14-bit duty
    TEST_ASSERT_EQUAL_UINT32(409, ServoOut::angle_to_duty(0.0f));
    TEST_ASSERT_EQUAL_UINT32(1187, ServoOut::angle_to_duty(90.0f));
    TEST_ASSERT_EQUAL_UINT32(1966, ServoOut::angle_to_duty(180.0f));

    // Out-of-bounds angles clamp to duty bounds
    TEST_ASSERT_EQUAL_UINT32(409, ServoOut::angle_to_duty(-30.0f));
    TEST_ASSERT_EQUAL_UINT32(1966, ServoOut::angle_to_duty(210.0f));
}

void test_servo_out_update_lifecycle(void) {
    ServoOut servo;
    servo.begin();

    // Initial: inactive
    TEST_ASSERT_FALSE(servo.is_pwm_active());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_right_deg());

    // Update with is_pwm_active = false while already inactive remains inactive
    servo.update(90.0f, 90.0f, false);
    TEST_ASSERT_FALSE(servo.is_pwm_active());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_right_deg());

    // Transition false -> true: attaches PWM and records written angles
    servo.update(90.0f, 90.0f, true);
    TEST_ASSERT_TRUE(servo.is_pwm_active());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.0f, servo.last_right_deg());

    // Transition true -> false: detaches, sets is_pwm_active_ = false, resets last angles
    servo.update(90.0f, 90.0f, false);
    TEST_ASSERT_FALSE(servo.is_pwm_active());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_right_deg());

    // Re-attach, then explicit detach()
    servo.update(120.0f, 60.0f, true);
    TEST_ASSERT_TRUE(servo.is_pwm_active());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 120.0f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.0f, servo.last_right_deg());

    servo.detach();
    TEST_ASSERT_FALSE(servo.is_pwm_active());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -999.0f, servo.last_right_deg());
}

void test_servo_out_micro_write_filter(void) {
    ServoOut servo;
    servo.begin();

    // Start with 90.0° active
    servo.update(90.00f, 90.00f, true);
    TEST_ASSERT_TRUE(servo.is_pwm_active());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.00f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.00f, servo.last_right_deg());

    // Delta < 0.12°: both skipped, last angles unchanged
    servo.update(90.05f, 90.10f, true);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.00f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.00f, servo.last_right_deg());

    // Edge check just below 0.12° threshold: delta = 0.119° -> skipped
    servo.update(90.119f, 89.881f, true);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.00f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.00f, servo.last_right_deg());

    // Delta >= 0.12°: exactly 0.12° delta -> recorded!
    servo.update(90.12f, 89.88f, true);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.12f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 89.88f, servo.last_right_deg());

    // Asymmetric update: left delta = 0.18° (>= 0.12°) recorded; right delta = 0.05° (< 0.12°) skipped
    servo.update(90.30f, 89.93f, true);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.30f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 89.88f, servo.last_right_deg());

    // Opposite asymmetric: left delta = 0.02° (< 0.12°) skipped; right delta = 0.38° (>= 0.12°) recorded
    servo.update(90.32f, 89.50f, true);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.30f, servo.last_left_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 89.50f, servo.last_right_deg());
}

void test_servo_out_pin_swap(void) {
    // Default (swap = false): left is PIN_SERVO_LEFT, right is PIN_SERVO_RIGHT
    ServoOut normal_servo(false);
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_LEFT, normal_servo.left_pin());
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_RIGHT, normal_servo.right_pin());

    // Swapped (swap = true): left is PIN_SERVO_RIGHT, right is PIN_SERVO_LEFT
    ServoOut swapped_servo(true);
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_RIGHT, swapped_servo.left_pin());
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_LEFT, swapped_servo.right_pin());
}

int run_unity_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_servo_out_begin_and_initial_state);
    RUN_TEST(test_servo_out_angle_to_pulse_us);
    RUN_TEST(test_servo_out_pulse_us_to_duty);
    RUN_TEST(test_servo_out_angle_to_duty);
    RUN_TEST(test_servo_out_update_lifecycle);
    RUN_TEST(test_servo_out_micro_write_filter);
    RUN_TEST(test_servo_out_pin_swap);
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
