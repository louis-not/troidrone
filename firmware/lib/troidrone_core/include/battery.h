#pragma once
#ifndef TROIDRONE_BATTERY_H_
#define TROIDRONE_BATTERY_H_

#include <cstdint>
#include <cmath>
#include <algorithm>

enum class BatteryState {
    OK = 0,
    WARNING,
    CRITICAL,
    CUTOFF
};

struct BatteryParams {
    float divider_r_top_kohm = 51.0f;
    float divider_r_bottom_kohm = 20.0f;
    float ema_rise_alpha = 0.12f;
    float ema_fall_alpha = 0.025f;
    float warning_threshold_v = 7.40f;
    float critical_threshold_v = 7.00f;
    float cutoff_threshold_v = 6.40f;
    float cutoff_hold_time_s = 2.00f;
    uint8_t cell_count = 2;
};

class BatteryMonitor {
public:
    explicit BatteryMonitor(const BatteryParams& params = BatteryParams{});

    void set_params(const BatteryParams& p);
    const BatteryParams& params() const;

    // Process instantaneous reading (in millivolts measured at ADC pin or directly as pack volts)
    void update_adc_mv(uint32_t adc_mv, float dt_s);
    void update_pack_v(float instantaneous_pack_v, float dt_s);

    float voltage() const { return filtered_v_; }
    uint8_t percentage() const;
    BatteryState state() const;
    bool is_cutoff_latched() const { return cutoff_latched_; }
    bool is_ok() const { return !cutoff_latched_; }

    void set_voltage(float v) {
        filtered_v_ = v;
        initialized_ = true;
        if (filtered_v_ >= params_.cutoff_threshold_v) {
            cutoff_timer_s_ = 0.0f;
        }
    }

    void force_cutoff() { cutoff_latched_ = true; }
    void reset();

    static float adc_mv_to_pack_v(uint32_t adc_mv, float r_top_kohm = 51.0f, float r_bottom_kohm = 20.0f);
    static uint8_t cell_voltage_to_percentage(float cell_v);

private:
    BatteryParams params_;
    float filtered_v_ = 0.0f;
    bool initialized_ = false;
    float cutoff_timer_s_ = 0.0f;
    bool cutoff_latched_ = false;
};

namespace troidrone {
    using ::BatteryState;
    using ::BatteryParams;
    using ::BatteryMonitor;
}

#endif  // TROIDRONE_BATTERY_H_
