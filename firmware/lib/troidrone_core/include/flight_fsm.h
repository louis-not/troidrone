#pragma once
#ifndef TROIDRONE_FLIGHT_FSM_H_
#define TROIDRONE_FLIGHT_FSM_H_

#include <cstdint>
#include <algorithm>

enum class FlightState {
    DISARMED,
    ARMED,
    FAILSAFE
};

class FlightFsm {
public:
    FlightFsm();

    FlightState state() const { return state_; }
    const char* state_str() const;
    static const char* to_string(FlightState s);

    void on_ctl(uint32_t now_ms, float thr, float str, uint32_t seq);
    bool on_arm(uint32_t now_ms, bool v);
    void on_link_closed();
    void step(uint32_t now_ms);

    uint32_t age_ms(uint32_t now_ms) const;
    float effective_thr() const;
    float last_throttle() const { return last_thr_; }
    float last_steering() const { return last_str_; }
    uint32_t last_seq() const { return last_seq_; }
    bool has_ctl() const { return has_ctl_; }
    uint32_t last_ctl_ms() const { return last_ctl_ms_; }

    void reset();

private:
    FlightState state_ = FlightState::DISARMED;
    bool has_ctl_ = false;
    uint32_t last_ctl_ms_ = 0;
    float last_thr_ = 0.0f;
    float last_str_ = 0.0f;
    uint32_t last_seq_ = 0;
};

namespace troidrone {
    using ::FlightState;
    using ::FlightFsm;
}

#endif  // TROIDRONE_FLIGHT_FSM_H_
