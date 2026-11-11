#pragma once
#ifndef TROIDRONE_LEDS_H_
#define TROIDRONE_LEDS_H_

#include <cstdint>
#include "pins.h"
#include "flight_fsm.h"
#include "battery.h"

class LedDriver {
public:
    void begin();
    void update(uint32_t now_ms, FlightState flight_state, BatteryState battery_state);

private:
    uint32_t last_warning_toggle_ms_ = 0;
    uint32_t last_critical_toggle_ms_ = 0;
    bool warning_led_state_ = false;
    bool critical_led_state_ = false;
};

#endif  // TROIDRONE_LEDS_H_
