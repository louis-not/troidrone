#include "flap_engine.h"

FlapEngine::FlapEngine(const FlapParams& params)
    : params_(params), phase_(0.75f), idle_lean_(0.0f) {
}

void FlapEngine::set_params(const FlapParams& p) {
    params_ = p;
    if (p.amp_deg != 75.0f && p.amp_up_deg == 75.0f && p.amp_dn_deg == 75.0f) {
        params_.amp_up_deg = p.amp_deg;
        params_.amp_dn_deg = p.amp_deg;
    }
}

const FlapParams& FlapEngine::params() const {
    return params_;
}

void FlapEngine::reset() {
    phase_ = 0.75f;
    idle_lean_ = 0.0f;
}

FlapOutput FlapEngine::update(float dt_s, bool armed, float thr, float str) {
    float clamped_str = std::clamp(str, -1.0f, 1.0f);

    if (!armed || thr <= params_.deadband) {
        phase_ = 0.75f;
        float target_lean = clamped_str * params_.idle_lean_max_deg;
        if (dt_s > 0.0f && params_.idle_lean_tau_s > 0.0f) {
            float alpha = 1.0f - std::exp(-dt_s / params_.idle_lean_tau_s);
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            idle_lean_ += alpha * (target_lean - idle_lean_);
        } else if (dt_s <= 0.0f) {
            if (params_.idle_lean_tau_s <= 0.0f) idle_lean_ = target_lean;
        } else {
            idle_lean_ = target_lean;
        }

        float left = std::clamp(params_.center_deg - idle_lean_, params_.min_deg, params_.max_deg);
        float right = std::clamp((180.0f - params_.center_deg) - idle_lean_, params_.min_deg, params_.max_deg);
        return FlapOutput{left, right, 0.0f, phase_};
    }

    // Armed and thr > deadband: ease idle lean to 0
    if (dt_s > 0.0f && params_.idle_lean_tau_s > 0.0f) {
        float alpha = 1.0f - std::exp(-dt_s / params_.idle_lean_tau_s);
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        idle_lean_ += alpha * (0.0f - idle_lean_);
    } else {
        idle_lean_ = 0.0f;
    }

    float clamped_thr = std::clamp(thr, 0.0f, 1.0f);
    float denom = 1.0f - params_.deadband;
    float freq_hz = params_.f_min_hz;
    if (denom > 0.0f) {
        freq_hz += ((clamped_thr - params_.deadband) / denom) * (params_.f_max_hz - params_.f_min_hz);
    }

    if (dt_s > 0.0f) {
        phase_ = std::fmod(phase_ + freq_hz * dt_s, 1.0f);
        if (phase_ < 0.0f) {
            phase_ += 1.0f;
        }
    }

    // Waveform calculation
    float w = 0.0f;
    if (params_.waveform == FlapWaveform::TRIANGLE) {
        w = 1.0f - 4.0f * std::fabs(phase_ - 0.5f);
    } else {
        constexpr float TWO_PI = 6.28318530717958647692f;
        w = -std::cos(TWO_PI * phase_);
    }

    // Asymmetric amplitude travel
    float travel = (w >= 0.0f) ? (params_.amp_up_deg * w) : (params_.amp_dn_deg * w);

    // Differential steering mixer
    float authority = std::min(70.0f, 10.0f + clamped_thr * clamped_thr * 50.0f);
    float s = (clamped_str * authority) / 70.0f;
    float base = travel * (1.0f + std::fabs(s) * params_.turn_comp);
    float delta = s * params_.steer_gain * base * 0.25f;
    float max_delta = 0.25f * std::fabs(base);
    delta = std::clamp(delta, -max_delta, max_delta);

    float left_defl = base + delta;
    float right_defl = base - delta;

    float left = std::clamp(params_.center_deg + left_defl, params_.min_deg, params_.max_deg);
    float right = std::clamp((180.0f - params_.center_deg) - right_defl, params_.min_deg, params_.max_deg);

    return FlapOutput{left, right, freq_hz, phase_};
}
