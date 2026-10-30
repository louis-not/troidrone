#pragma once
#ifndef TROIDRONE_SERVO_OUT_H_
#define TROIDRONE_SERVO_OUT_H_

#include <cstdint>
#include <algorithm>
#include "pins.h"
#include "servo_ease.h"

#if defined(ARDUINO)
    #include <Arduino.h>
#endif

class ServoOut {
public:
    explicit ServoOut(bool swap = SERVO_SWAP);

    void begin();
    void update(float left_deg, float right_deg, bool is_pwm_active);
    void detach();

    bool is_pwm_active() const { return is_pwm_active_; }
    float last_left_deg() const { return last_left_deg_; }
    float last_right_deg() const { return last_right_deg_; }

    uint8_t left_pin() const { return left_pin_; }
    uint8_t right_pin() const { return right_pin_; }

    static uint32_t angle_to_pulse_us(float deg);
    static uint32_t pulse_us_to_duty(uint32_t pulse_us);
    static uint32_t angle_to_duty(float deg);

private:
    uint8_t left_pin_;
    uint8_t right_pin_;
    bool is_pwm_active_ = false;
    float last_left_deg_ = -999.0f;
    float last_right_deg_ = -999.0f;

    void write_left_deg(float deg);
    void write_right_deg(float deg);
};

#endif  // TROIDRONE_SERVO_OUT_H_
