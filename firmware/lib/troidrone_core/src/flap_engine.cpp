#include "flap_engine.h"

FlapEngine::FlapEngine(const FlapParams& params)
    : params_(params), phase_(0.75f) {
}

void FlapEngine::set_params(const FlapParams& p) {
    params_ = p;
}

const FlapParams& FlapEngine::params() const {
    return params_;
}

void FlapEngine::reset() {
    phase_ = 0.75f;
}

FlapOutput FlapEngine::update(float dt_s, bool armed, float thr) {
    if (!armed || thr <= params_.deadband) {
        phase_ = 0.75f;
        float left = std::clamp(params_.center_deg, params_.min_deg, params_.max_deg);
        float right = std::clamp(180.0f - params_.center_deg, params_.min_deg, params_.max_deg);
        return FlapOutput{left, right, 0.0f, phase_};
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

    float w = 1.0f - 4.0f * std::fabs(phase_ - 0.5f);
    float left = std::clamp(params_.center_deg + params_.amp_deg * w, params_.min_deg, params_.max_deg);
    float right = std::clamp((180.0f - params_.center_deg) - params_.amp_deg * w, params_.min_deg, params_.max_deg);

    return FlapOutput{left, right, freq_hz, phase_};
}
