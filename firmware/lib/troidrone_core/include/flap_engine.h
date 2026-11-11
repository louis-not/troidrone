#pragma once
#ifndef TROIDRONE_FLAP_ENGINE_H_
#define TROIDRONE_FLAP_ENGINE_H_

#include <cmath>
#include <algorithm>

enum class FlapWaveform {
    TRIANGLE = 0,
    SINE = 1
};

struct FlapParams {
    float center_deg = 90.0f;
    float amp_deg = 75.0f;
    float amp_up_deg = 75.0f;
    float amp_dn_deg = 75.0f;
    float steer_gain = 1.15f;
    float turn_comp = 0.12f;
    float idle_lean_max_deg = 15.0f;
    float idle_lean_tau_s = 0.045f;
    FlapWaveform waveform = FlapWaveform::TRIANGLE;
    float f_min_hz = 1.0f;
    float f_max_hz = 4.0f;
    float deadband = 0.05f;
    float min_deg = 0.0f;
    float max_deg = 180.0f;
};

struct FlapOutput {
    float left_deg;
    float right_deg;
    float freq_hz;
    float phase;
};

class FlapEngine {
public:
    explicit FlapEngine(const FlapParams& params = FlapParams{});

    void set_params(const FlapParams& p);
    const FlapParams& params() const;

    FlapOutput update(float dt_s, bool armed, float thr, float str = 0.0f);
    void reset();

    float phase() const { return phase_; }
    float idle_lean() const { return idle_lean_; }

private:
    FlapParams params_;
    float phase_ = 0.75f;
    float idle_lean_ = 0.0f;
};

namespace troidrone {
    using ::FlapWaveform;
    using ::FlapParams;
    using ::FlapOutput;
    using ::FlapEngine;
}

#endif  // TROIDRONE_FLAP_ENGINE_H_
