#include "flight_fsm.h"

FlightFsm::FlightFsm()
    : state_(FlightState::DISARMED),
      has_ctl_(false),
      last_ctl_ms_(0),
      last_thr_(0.0f),
      last_str_(0.0f),
      last_seq_(0) {
}

const char* FlightFsm::to_string(FlightState s) {
    switch (s) {
        case FlightState::DISARMED: return "DISARMED";
        case FlightState::ARMED:    return "ARMED";
        case FlightState::FAILSAFE: return "FAILSAFE";
    }
    return "DISARMED";
}

const char* FlightFsm::state_str() const {
    return to_string(state_);
}

void FlightFsm::on_ctl(uint32_t now_ms, float thr, float str, uint32_t seq) {
    has_ctl_ = true;
    last_ctl_ms_ = now_ms;
    last_thr_ = std::clamp(thr, 0.0f, 1.0f);
    last_str_ = std::clamp(str, -1.0f, 1.0f);
    last_seq_ = seq;
}

bool FlightFsm::on_arm(uint32_t now_ms, bool v) {
    if (v) {
        // v == true: transitions DISARMED -> ARMED ONLY IF has_ctl, (now_ms - last_ctl_ms) <= 500, and last_thr < 0.05f.
        // Ignored in FAILSAFE or if checks fail. Returns true if transition occurred, false otherwise.
        if (state_ == FlightState::DISARMED) {
            if (has_ctl_ && ((now_ms - last_ctl_ms_) <= 500u) && (last_thr_ < 0.05f)) {
                state_ = FlightState::ARMED;
                return true;
            }
        }
        return false;
    } else {
        // v == false: transitions ARMED -> DISARMED or FAILSAFE -> DISARMED.
        // If DISARMED, stays DISARMED. Returns true if changed, false otherwise.
        if (state_ == FlightState::ARMED || state_ == FlightState::FAILSAFE) {
            state_ = FlightState::DISARMED;
            return true;
        }
        return false;
    }
}

void FlightFsm::on_link_closed() {
    has_ctl_ = false;
    if (state_ == FlightState::ARMED) {
        state_ = FlightState::FAILSAFE;
    }
}

void FlightFsm::step(uint32_t now_ms) {
    if (state_ == FlightState::ARMED) {
        if (!has_ctl_ || ((now_ms - last_ctl_ms_) > 500u)) {
            state_ = FlightState::FAILSAFE;
        }
    }
}

uint32_t FlightFsm::age_ms(uint32_t now_ms) const {
    return has_ctl_ ? (now_ms - last_ctl_ms_) : 9999u;
}

float FlightFsm::effective_thr() const {
    return (state_ == FlightState::ARMED) ? last_thr_ : 0.0f;
}

void FlightFsm::reset() {
    state_ = FlightState::DISARMED;
    has_ctl_ = false;
    last_ctl_ms_ = 0;
    last_thr_ = 0.0f;
    last_str_ = 0.0f;
    last_seq_ = 0;
}
