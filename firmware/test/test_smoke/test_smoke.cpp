#include <unity.h>
#include "pins.h"

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Teardown after each test
}

void test_servo_pins_default(void) {
    TEST_ASSERT_EQUAL_UINT8(D0, PIN_SERVO_LEFT);
    TEST_ASSERT_EQUAL_UINT8(D1, PIN_SERVO_RIGHT);
    TEST_ASSERT_FALSE(SERVO_SWAP);
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_LEFT, get_left_servo_pin());
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_RIGHT, get_right_servo_pin());
}

void test_servo_pins_swap_resolution(void) {
    // Test unswapped explicitly
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_LEFT, get_left_servo_pin(false));
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_RIGHT, get_right_servo_pin(false));

    // Test swapped explicitly
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_RIGHT, get_left_servo_pin(true));
    TEST_ASSERT_EQUAL_UINT8(PIN_SERVO_LEFT, get_right_servo_pin(true));
}

void test_servo_pwm_constants(void) {
    TEST_ASSERT_EQUAL_UINT32(50, SERVO_FREQ_HZ);
    TEST_ASSERT_EQUAL_UINT32(500, SERVO_MIN_US);
    TEST_ASSERT_EQUAL_UINT32(2400, SERVO_MAX_US);
    TEST_ASSERT_EQUAL_UINT32(1450, SERVO_NEUTRAL_US);
    TEST_ASSERT_TRUE(SERVO_MIN_US < SERVO_NEUTRAL_US);
    TEST_ASSERT_TRUE(SERVO_NEUTRAL_US < SERVO_MAX_US);
}

void test_peripheral_pin_assignments(void) {
    TEST_ASSERT_EQUAL_UINT8(D2, PIN_VBAT_ADC);
    TEST_ASSERT_EQUAL_UINT8(D3, PIN_WING_LED_MOSFET);
    TEST_ASSERT_EQUAL_UINT8(D6, PIN_LED_STATUS);
    TEST_ASSERT_EQUAL_UINT8(D8, PIN_LED_LOWBATT);
}

int run_unity_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_servo_pins_default);
    RUN_TEST(test_servo_pins_swap_resolution);
    RUN_TEST(test_servo_pwm_constants);
    RUN_TEST(test_peripheral_pin_assignments);
    return UNITY_END();
}

#if defined(ARDUINO)
void setup() {
    delay(2000);
    run_unity_tests();
}
void loop() {}
#else
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return run_unity_tests();
}
#endif
