#pragma once
#ifndef TROIDRONE_PINS_H_
#define TROIDRONE_PINS_H_

#include <cstdint>

#if defined(ARDUINO)
    #include <Arduino.h>
#else
    // Fallback pin definitions for native/unit testing environment where Arduino headers are not available.
    #ifndef D0
    inline constexpr uint8_t D0 = 2;
    #endif
    #ifndef D1
    inline constexpr uint8_t D1 = 3;
    #endif
    #ifndef D2
    inline constexpr uint8_t D2 = 4;
    #endif
    #ifndef D3
    inline constexpr uint8_t D3 = 5;
    #endif
    #ifndef D6
    inline constexpr uint8_t D6 = 20;
    #endif
    #ifndef D8
    inline constexpr uint8_t D8 = 8;
    #endif
    #ifndef D9
    inline constexpr uint8_t D9 = 9;
    #endif
    #ifndef D10
    inline constexpr uint8_t D10 = 10;
    #endif
#endif

// ============================================================================
// Contract C2: Hardware Pinout & Peripherals for Seeed Studio XIAO ESP32-C3
// ============================================================================

// Servos (Elevons)
inline constexpr uint8_t PIN_SERVO_LEFT = D0;
inline constexpr uint8_t PIN_SERVO_RIGHT = D1;

// Hardware servo swap configuration flag
inline constexpr bool SERVO_SWAP = false;

// Helper functions for servo pin resolution based on swap flag
inline constexpr uint8_t get_left_servo_pin(bool swap = SERVO_SWAP) {
    return swap ? PIN_SERVO_RIGHT : PIN_SERVO_LEFT;
}

inline constexpr uint8_t get_right_servo_pin(bool swap = SERVO_SWAP) {
    return swap ? PIN_SERVO_LEFT : PIN_SERVO_RIGHT;
}

// Battery voltage ADC sensing
inline constexpr uint8_t PIN_VBAT_ADC = D2;

// Wing LED MOSFET control
inline constexpr uint8_t PIN_WING_LED_MOSFET = D3;

// Status & Alert LEDs
inline constexpr uint8_t PIN_LED_STATUS = D6;
inline constexpr uint8_t PIN_LED_LOWBATT = D8;

// RESERVED PINS:
// D9 (GPIO9) and D10 (GPIO10) are reserved for ELRS receiver UART.
// DO NOT touch or reassign D9/D10 for general GPIO or other peripherals.

// Servo PWM Constants
inline constexpr uint32_t SERVO_FREQ_HZ = 50;
inline constexpr uint32_t SERVO_MIN_US = 500;
inline constexpr uint32_t SERVO_MAX_US = 2400;
inline constexpr uint32_t SERVO_NEUTRAL_US = 1450;

#endif  // TROIDRONE_PINS_H_
