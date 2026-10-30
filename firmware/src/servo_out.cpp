#include "servo_out.h"
#include <cmath>

ServoOut::ServoOut(bool swap)
    : left_pin_(get_left_servo_pin(swap)),
      right_pin_(get_right_servo_pin(swap)),
      is_pwm_active_(false),
      last_left_deg_(-999.0f),
      last_right_deg_(-999.0f) {
}

void ServoOut::begin() {
#if defined(ARDUINO)
    pinMode(left_pin_, OUTPUT);
    digitalWrite(left_pin_, LOW);
    pinMode(right_pin_, OUTPUT);
    digitalWrite(right_pin_, LOW);
#endif
    is_pwm_active_ = false;
    last_left_deg_ = -999.0f;
    last_right_deg_ = -999.0f;
}

void ServoOut::detach() {
#if defined(ARDUINO)
    ledcDetach(left_pin_);
    ledcDetach(right_pin_);
    pinMode(left_pin_, OUTPUT);
    digitalWrite(left_pin_, LOW);
    pinMode(right_pin_, OUTPUT);
    digitalWrite(right_pin_, LOW);
#endif
    is_pwm_active_ = false;
    last_left_deg_ = -999.0f;
    last_right_deg_ = -999.0f;
}

uint32_t ServoOut::angle_to_pulse_us(float deg) {
    float clamped = std::clamp(deg, 0.0f, 180.0f);
    float pulse = static_cast<float>(SERVO_MIN_US) +
                  (clamped / 180.0f) * static_cast<float>(SERVO_MAX_US - SERVO_MIN_US);
    return static_cast<uint32_t>(pulse + 0.5f);
}

uint32_t ServoOut::pulse_us_to_duty(uint32_t pulse_us) {
    // 50 Hz PWM period is 20,000 us.
    // 14-bit resolution: duty = (pulse_us * 16384) / 20000.
    return (pulse_us * 16384UL) / 20000UL;
}

uint32_t ServoOut::angle_to_duty(float deg) {
    return pulse_us_to_duty(angle_to_pulse_us(deg));
}

void ServoOut::write_left_deg(float deg) {
    float clamped = std::clamp(deg, 0.0f, 180.0f);
#if defined(ARDUINO)
    uint32_t duty = angle_to_duty(clamped);
    ledcWrite(left_pin_, duty);
#endif
    last_left_deg_ = clamped;
}

void ServoOut::write_right_deg(float deg) {
    float clamped = std::clamp(deg, 0.0f, 180.0f);
#if defined(ARDUINO)
    uint32_t duty = angle_to_duty(clamped);
    ledcWrite(right_pin_, duty);
#endif
    last_right_deg_ = clamped;
}

void ServoOut::update(float left_deg, float right_deg, bool is_pwm_active) {
    if (!is_pwm_active) {
        if (is_pwm_active_) {
            detach();
        }
        return;
    }

    if (!is_pwm_active_) {
#if defined(ARDUINO)
        ledcAttach(left_pin_, SERVO_FREQ_HZ, 14);
        ledcAttach(right_pin_, SERVO_FREQ_HZ, 14);
#endif
        is_pwm_active_ = true;
        write_left_deg(left_deg);
        write_right_deg(right_deg);
        return;
    }

    // Micro-write filter: skip servo write if difference from last written angle is < 0.12 deg
    if (should_write_servo(left_deg, last_left_deg_)) {
        write_left_deg(left_deg);
    }
    if (should_write_servo(right_deg, last_right_deg_)) {
        write_right_deg(right_deg);
    }
}
