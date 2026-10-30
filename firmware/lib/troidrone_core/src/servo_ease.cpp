#include "servo_ease.h"
#include <algorithm>

static inline float step_towards(float current, float target, float max_delta) {
    if (std::fabs(target - current) <= max_delta) {
        return target;
    }
    return current + std::copysign(max_delta, target - current);
}

ServoEase::ServoEase(float slew_rate_deg_per_s, float ramp_duration_s, float center_deg)
    : slew_rate_deg_per_s_(slew_rate_deg_per_s),
      ramp_duration_s_(ramp_duration_s),
      center_deg_(center_deg),
      current_left_deg_(center_deg),
      current_right_deg_(center_deg) {
}

void ServoEase::reset() {
    state_ = ServoEaseState::IDLE_LOW;
    current_left_deg_ = center_deg_;
    current_right_deg_ = center_deg_;
    ramp_elapsed_s_ = 0.0f;
}

void ServoEase::update(float dt_s, FlightState state, float target_left_deg, float target_right_deg,
                       float& out_left_deg, float& out_right_deg, bool& out_pwm_active) {
    if (dt_s < 0.0f) {
        dt_s = 0.0f;
    }
    float max_delta = slew_rate_deg_per_s_ * dt_s;

    switch (state_) {
        case ServoEaseState::IDLE_LOW: {
            if (state == FlightState::ARMED) {
                state_ = ServoEaseState::ARMING_RAMP;
                ramp_elapsed_s_ = 0.0f;
                out_pwm_active = true;
                current_left_deg_ = step_towards(current_left_deg_, target_left_deg, max_delta);
                current_right_deg_ = step_towards(current_right_deg_, target_right_deg, max_delta);
                ramp_elapsed_s_ += dt_s;
                if (ramp_elapsed_s_ >= ramp_duration_s_) {
                    state_ = ServoEaseState::ACTIVE_PWM;
                    current_left_deg_ = target_left_deg;
                    current_right_deg_ = target_right_deg;
                }
            } else {
                current_left_deg_ = center_deg_;
                current_right_deg_ = center_deg_;
                out_pwm_active = false;
            }
            break;
        }

        case ServoEaseState::ARMING_RAMP: {
            if (state != FlightState::ARMED) {
                state_ = ServoEaseState::DISARMING_RAMP;
                ramp_elapsed_s_ = 0.0f;
                out_pwm_active = true;
                current_left_deg_ = step_towards(current_left_deg_, center_deg_, max_delta);
                current_right_deg_ = step_towards(current_right_deg_, center_deg_, max_delta);
                ramp_elapsed_s_ += dt_s;
                if (ramp_elapsed_s_ >= ramp_duration_s_) {
                    state_ = ServoEaseState::IDLE_LOW;
                    current_left_deg_ = center_deg_;
                    current_right_deg_ = center_deg_;
                    out_pwm_active = false;
                }
            } else {
                out_pwm_active = true;
                current_left_deg_ = step_towards(current_left_deg_, target_left_deg, max_delta);
                current_right_deg_ = step_towards(current_right_deg_, target_right_deg, max_delta);
                ramp_elapsed_s_ += dt_s;
                if (ramp_elapsed_s_ >= ramp_duration_s_) {
                    state_ = ServoEaseState::ACTIVE_PWM;
                    current_left_deg_ = target_left_deg;
                    current_right_deg_ = target_right_deg;
                }
            }
            break;
        }

        case ServoEaseState::ACTIVE_PWM: {
            if (state != FlightState::ARMED) {
                state_ = ServoEaseState::DISARMING_RAMP;
                ramp_elapsed_s_ = 0.0f;
                out_pwm_active = true;
                current_left_deg_ = step_towards(current_left_deg_, center_deg_, max_delta);
                current_right_deg_ = step_towards(current_right_deg_, center_deg_, max_delta);
                ramp_elapsed_s_ += dt_s;
                if (ramp_elapsed_s_ >= ramp_duration_s_) {
                    state_ = ServoEaseState::IDLE_LOW;
                    current_left_deg_ = center_deg_;
                    current_right_deg_ = center_deg_;
                    out_pwm_active = false;
                }
            } else {
                out_pwm_active = true;
                current_left_deg_ = target_left_deg;
                current_right_deg_ = target_right_deg;
            }
            break;
        }

        case ServoEaseState::DISARMING_RAMP: {
            if (state == FlightState::ARMED) {
                state_ = ServoEaseState::ARMING_RAMP;
                ramp_elapsed_s_ = 0.0f;
                out_pwm_active = true;
                current_left_deg_ = step_towards(current_left_deg_, target_left_deg, max_delta);
                current_right_deg_ = step_towards(current_right_deg_, target_right_deg, max_delta);
                ramp_elapsed_s_ += dt_s;
                if (ramp_elapsed_s_ >= ramp_duration_s_) {
                    state_ = ServoEaseState::ACTIVE_PWM;
                    current_left_deg_ = target_left_deg;
                    current_right_deg_ = target_right_deg;
                }
            } else {
                out_pwm_active = true;
                current_left_deg_ = step_towards(current_left_deg_, center_deg_, max_delta);
                current_right_deg_ = step_towards(current_right_deg_, center_deg_, max_delta);
                ramp_elapsed_s_ += dt_s;
                if (ramp_elapsed_s_ >= ramp_duration_s_) {
                    state_ = ServoEaseState::IDLE_LOW;
                    current_left_deg_ = center_deg_;
                    current_right_deg_ = center_deg_;
                    out_pwm_active = false;
                }
            }
            break;
        }
    }

    out_left_deg = current_left_deg_;
    out_right_deg = current_right_deg_;
}
