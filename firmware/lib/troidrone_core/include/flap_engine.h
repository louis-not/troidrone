#pragma once
#ifndef TROIDRONE_FLAP_ENGINE_H_
#define TROIDRONE_FLAP_ENGINE_H_

#include <cmath>
#include <algorithm>

struct FlapParams {
    float center_deg = 90.0f;
    float amp_deg = 75.0f;
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

    FlapOutput update(float dt_s, bool armed, float thr);
    void reset();

    float phase() const { return phase_; }

private:
    FlapParams params_;
    float phase_ = 0.75f;
};

namespace troidrone {
    using ::FlapParams;
    using ::FlapOutput;
    using ::FlapEngine;
}

#endif  // TROIDRONE_FLAP_ENGINE_H_
