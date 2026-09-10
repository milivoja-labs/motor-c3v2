// =============================================================================
//  power_curve.cpp — implementation of the throttle -> PPM mapping
// =============================================================================
//  Note: the ESP32-C3 (RISC-V) has no hardware FPU, so powf() is software
//  emulated. At a 50 Hz evaluation rate this is completely negligible. If you
//  ever want to remove float entirely, the same curves can be done with
//  fixed-point or a small lookup table — the interface would not change.
// =============================================================================
#include "power_curve.hpp"

#include <algorithm>
#include <cmath>

namespace motor {

namespace {

float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// smoothstep: 0 at 0, 1 at 1, flat slope at both ends (S-curve)
float smoothstep(float x)
{
    return x * x * (3.0f - 2.0f * x);
}

// Shape a normalized throttle x in [0,1] to a normalized output in [0,1].
float shape(float x, CurveMode mode, float expo)
{
    x    = clamp01(x);
    expo = clamp01(expo);

    switch (mode) {
        case CurveMode::LINEAR:
            return x;

        case CurveMode::SOFT_START: {
            // Convex power curve: expo 0 -> linear, expo 1 -> x^3.
            const float p = 1.0f + 2.0f * expo;
            return clamp01(std::pow(x, p));
        }

        case CurveMode::PROGRESSIVE: {
            // Blend linear toward an S-curve as expo rises.
            const float s = smoothstep(x);
            return clamp01(x + expo * (s - x));
        }
    }
    return x;
}

}  // namespace

uint32_t map_throttle_to_ppm_us(uint8_t throttle, const PowerCurveConfig& cfg)
{
    // Clamp throttle to protocol range.
    if (throttle > 100) {
        throttle = 100;
    }

    // Deadband -> SAFE / neutral.
    if (throttle <= cfg.deadband_pct) {
        return cfg.neutral_us;
    }

    // Normalize (deadband .. 100] -> (0 .. 1].
    const float span = static_cast<float>(100 - cfg.deadband_pct);
    const float x    = (span > 0.0f)
                         ? static_cast<float>(throttle - cfg.deadband_pct) / span
                         : 1.0f;

    const float y = shape(x, cfg.mode, cfg.expo);

    // Interpolate output pulse.
    const float lo    = static_cast<float>(cfg.out_min_us);
    const float hi    = static_cast<float>(cfg.out_max_us);
    const float pulse = lo + (hi - lo) * y;

    // Round and clamp to the band (handles out_min > out_max gracefully).
    uint32_t result = static_cast<uint32_t>(pulse + 0.5f);
    const uint32_t band_lo = std::min(cfg.out_min_us, cfg.out_max_us);
    const uint32_t band_hi = std::max(cfg.out_min_us, cfg.out_max_us);
    result = std::max(band_lo, std::min(band_hi, result));
    return result;
}

}  // namespace motor
