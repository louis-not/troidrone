#include "leds.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

void LedDriver::begin() {
#if defined(ARDUINO)
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);
    pinMode(PIN_LED_LOWBATT, OUTPUT);
    digitalWrite(PIN_LED_LOWBATT, LOW);
#endif
}

void LedDriver::update(uint32_t now_ms, FlightState flight_state, BatteryState battery_state) {
#if defined(ARDUINO)
    // 1. Status LED (D6): solid ON when ARMED, else OFF
    if (flight_state == FlightState::ARMED) {
        digitalWrite(PIN_LED_STATUS, HIGH);
    } else {
        digitalWrite(PIN_LED_STATUS, LOW);
    }

    // 2. Low-battery LED (D8):
    // OFF when OK, 1 Hz blink on WARNING (<7.4V), 2 Hz blink on CRITICAL (<7.0V), solid ON on CUTOFF (<6.4V latched)
    switch (battery_state) {
        case BatteryState::OK:
            digitalWrite(PIN_LED_LOWBATT, LOW);
            break;

        case BatteryState::WARNING: {
            // 1 Hz blink: 500 ms on, 500 ms off
            if (now_ms - last_warning_toggle_ms_ >= 500) {
                last_warning_toggle_ms_ = now_ms;
                warning_led_state_ = !warning_led_state_;
            }
            digitalWrite(PIN_LED_LOWBATT, warning_led_state_ ? HIGH : LOW);
            break;
        }

        case BatteryState::CRITICAL: {
            // 2 Hz blink: 250 ms on, 250 ms off
            if (now_ms - last_critical_toggle_ms_ >= 250) {
                last_critical_toggle_ms_ = now_ms;
                critical_led_state_ = !critical_led_state_;
            }
            digitalWrite(PIN_LED_LOWBATT, critical_led_state_ ? HIGH : LOW);
            break;
        }

        case BatteryState::CUTOFF:
            digitalWrite(PIN_LED_LOWBATT, HIGH);
            break;
    }
#else
    (void)now_ms;
    (void)flight_state;
    (void)battery_state;
#endif
}
