#pragma once
#ifndef TROIDRONE_SERVO_EASE_H_
#define TROIDRONE_SERVO_EASE_H_

#include "flight_fsm.h"
#include <cmath>

enum class ServoEaseState {
    IDLE_LOW,
    ARMING_RAMP,
    ACTIVE_PWM,
    DISARMING_RAMP
};

inline bool should_write_servo(float new_deg, float last_written_deg, float threshold = 0.12f) {
    return std::fabs(new_deg - last_written_deg) >= threshold;
}

class ServoEase {
public:
    explicit ServoEase(float slew_rate_deg_per_s = 300.0f,
                       float ramp_duration_s = 0.3f,
                       float center_deg = 90.0f);

    void update(float dt_s, FlightState state, float target_left_deg, float target_right_deg,
                float& out_left_deg, float& out_right_deg, bool& out_pwm_active);

    void reset();

    ServoEaseState state() const { return state_; }
    float slew_rate() const { return slew_rate_deg_per_s_; }
    void set_slew_rate(float rate) { slew_rate_deg_per_s_ = rate; }
    float ramp_duration() const { return ramp_duration_s_; }
    void set_ramp_duration(float d) { ramp_duration_s_ = d; }
    float center_deg() const { return center_deg_; }
    void set_center_deg(float c) { center_deg_ = c; }
    float current_left() const { return current_left_deg_; }
    float current_right() const { return current_right_deg_; }

    static bool should_write(float new_deg, float last_written_deg, float threshold = 0.12f) {
        return should_write_servo(new_deg, last_written_deg, threshold);
    }

private:
    ServoEaseState state_ = ServoEaseState::IDLE_LOW;
    float slew_rate_deg_per_s_ = 300.0f;
    float ramp_duration_s_ = 0.3f;
    float center_deg_ = 90.0f;
    float current_left_deg_ = 90.0f;
    float current_right_deg_ = 90.0f;
    float ramp_elapsed_s_ = 0.0f;
};

namespace troidrone {
    using ::ServoEaseState;
    using ::ServoEase;
    using ::should_write_servo;
}

#endif  // TROIDRONE_SERVO_EASE_H_
