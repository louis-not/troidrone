#include "battery.h"

BatteryMonitor::BatteryMonitor(const BatteryParams& params)
    : params_(params), filtered_v_(0.0f), initialized_(false), cutoff_timer_s_(0.0f), cutoff_latched_(false) {
}

void BatteryMonitor::set_params(const BatteryParams& p) {
    params_ = p;
}

const BatteryParams& BatteryMonitor::params() const {
    return params_;
}

void BatteryMonitor::reset() {
    filtered_v_ = 0.0f;
    initialized_ = false;
    cutoff_timer_s_ = 0.0f;
    cutoff_latched_ = false;
}

float BatteryMonitor::adc_mv_to_pack_v(uint32_t adc_mv, float r_top_kohm, float r_bottom_kohm) {
    if (r_bottom_kohm <= 0.0f) return 0.0f;
    float divider_gain = (r_top_kohm + r_bottom_kohm) / r_bottom_kohm;
    return (static_cast<float>(adc_mv) / 1000.0f) * divider_gain;
}

uint8_t BatteryMonitor::cell_voltage_to_percentage(float cell_v) {
    static const float lipo_table_v[] = {
        3.30f, 3.60f, 3.70f, 3.75f, 3.80f, 3.85f, 3.90f,
        3.95f, 4.00f, 4.05f, 4.10f, 4.15f, 4.20f
    };
    static const float pct_table[] = {
        0.00f, 0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f,
        0.70f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f
    };
    constexpr size_t COUNT = sizeof(lipo_table_v) / sizeof(lipo_table_v[0]);

    if (cell_v <= lipo_table_v[0]) return 0;
    if (cell_v >= lipo_table_v[COUNT - 1]) return 100;

    for (size_t i = 1; i < COUNT; ++i) {
        if (cell_v <= lipo_table_v[i]) {
            float span = lipo_table_v[i] - lipo_table_v[i - 1];
            float pos = (span > 0.0f) ? ((cell_v - lipo_table_v[i - 1]) / span) : 0.0f;
            float pct = pct_table[i - 1] + pos * (pct_table[i] - pct_table[i - 1]);
            return static_cast<uint8_t>(std::clamp(std::round(pct * 100.0f), 0.0f, 100.0f));
        }
    }
    return 100;
}

uint8_t BatteryMonitor::percentage() const {
    if (!initialized_ || params_.cell_count == 0) {
        return 0;
    }
    float cell_v = filtered_v_ / static_cast<float>(params_.cell_count);
    return cell_voltage_to_percentage(cell_v);
}

BatteryState BatteryMonitor::state() const {
    if (cutoff_latched_) {
        return BatteryState::CUTOFF;
    }
    if (!initialized_) {
        return BatteryState::OK;
    }
    if (filtered_v_ < params_.critical_threshold_v) {
        return BatteryState::CRITICAL;
    }
    if (filtered_v_ < params_.warning_threshold_v) {
        return BatteryState::WARNING;
    }
    return BatteryState::OK;
}

void BatteryMonitor::update_adc_mv(uint32_t adc_mv, float dt_s) {
    float pack_v = adc_mv_to_pack_v(adc_mv, params_.divider_r_top_kohm, params_.divider_r_bottom_kohm);
    update_pack_v(pack_v, dt_s);
}

void BatteryMonitor::update_pack_v(float instantaneous_pack_v, float dt_s) {
    if (!initialized_) {
        filtered_v_ = instantaneous_pack_v;
        initialized_ = true;
    } else {
        float alpha = (instantaneous_pack_v > filtered_v_) ? params_.ema_rise_alpha : params_.ema_fall_alpha;
        filtered_v_ += alpha * (instantaneous_pack_v - filtered_v_);
    }

    if (cutoff_latched_) {
        return;
    }

    if (filtered_v_ < params_.cutoff_threshold_v) {
        cutoff_timer_s_ += dt_s;
        if (cutoff_timer_s_ >= params_.cutoff_hold_time_s) {
            cutoff_latched_ = true;
        }
    } else {
        cutoff_timer_s_ = 0.0f;
    }
}
