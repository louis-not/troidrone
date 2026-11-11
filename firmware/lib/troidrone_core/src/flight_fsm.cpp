#include "flight_fsm.h"

FlightFsm::FlightFsm()
    : state_(FlightState::DISARMED),
      has_ctl_(false),
      last_ctl_ms_(0),
      last_thr_(0.0f),
      last_str_(0.0f),
      last_seq_(0),
      release_lock_until_ms_(0) {
}

const char* FlightFsm::to_string(FlightState s) {
    switch (s) {
        case FlightState::DISARMED: return "DISARMED";
        case FlightState::ARMED:    return "ARMED";
        case FlightState::FAILSAFE: return "FAILSAFE";
        case FlightState::LOWBATT:  return "LOWBATT";
    }
    return "DISARMED";
}

const char* FlightFsm::to_refusal_str(ArmRefusal r) {
    switch (r) {
        case ArmRefusal::NONE:          return "none";
        case ArmRefusal::BATT:          return "batt";
        case ArmRefusal::THROTTLE:      return "thr";
        case ArmRefusal::NO_LINK:       return "link";
        case ArmRefusal::INVALID_STATE: return "state";
    }
    return "unknown";
}

const char* FlightFsm::state_str() const {
    return to_string(state_);
}

void FlightFsm::on_ctl(uint32_t now_ms, float thr, float str, uint32_t seq) {
    has_ctl_ = true;
    last_ctl_ms_ = now_ms;
    last_seq_ = seq;
    last_str_ = std::clamp(str, -1.0f, 1.0f);

    // 250ms release lock: ignore non-idle throttle (> 0.05f)
    if (now_ms < release_lock_until_ms_ && thr > 0.05f) {
        last_thr_ = 0.0f;
    } else {
        last_thr_ = std::clamp(thr, 0.0f, 1.0f);
    }
}

bool FlightFsm::on_stop(uint32_t now_ms, uint32_t seq) {
    if (has_ctl_) {
        int32_t diff = static_cast<int32_t>(seq - last_seq_);
        if (diff <= 0) {
            return false;
        }
    }
    has_ctl_ = true;
    last_seq_ = seq;
    last_thr_ = 0.0f;
    release_lock_until_ms_ = now_ms + 250;
    // CRUCIAL: on_stop does NOT update last_ctl_ms_
    return true;
}

void FlightFsm::on_battery_cutoff() {
    state_ = FlightState::LOWBATT;
    last_thr_ = 0.0f;
}

ArmResult FlightFsm::on_arm_result(uint32_t now_ms, bool v, bool battery_ok) {
    if (v) {
        // v == true: Attempt to arm
        if (state_ == FlightState::LOWBATT || !battery_ok) {
            return ArmResult{false, ArmRefusal::BATT};
        }
        if (state_ != FlightState::DISARMED) {
            return ArmResult{false, ArmRefusal::INVALID_STATE};
        }
        if (!has_ctl_ || ((now_ms - last_ctl_ms_) > 500u)) {
            return ArmResult{false, ArmRefusal::NO_LINK};
        }
        if (last_thr_ >= 0.05f) {
            return ArmResult{false, ArmRefusal::THROTTLE};
        }
        state_ = FlightState::ARMED;
        return ArmResult{true, ArmRefusal::NONE};
    } else {
        // v == false: Disarm
        if (state_ == FlightState::LOWBATT) {
            // LOWBATT is latched until reboot
            return ArmResult{false, ArmRefusal::BATT};
        }
        if (state_ == FlightState::ARMED || state_ == FlightState::FAILSAFE) {
            state_ = FlightState::DISARMED;
            return ArmResult{true, ArmRefusal::NONE};
        }
        return ArmResult{false, ArmRefusal::NONE};
    }
}

bool FlightFsm::on_arm(uint32_t now_ms, bool v, bool battery_ok) {
    return on_arm_result(now_ms, v, battery_ok).success;
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
    release_lock_until_ms_ = 0;
}
