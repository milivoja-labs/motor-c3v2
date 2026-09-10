// =============================================================================
//  power_curve.hpp — Pure, stateless throttle -> PPM pulse mapping
// =============================================================================
//
//  This module is deliberately free of BLE, FreeRTOS and ESP-IDF dependencies
//  so you can change the feel of the throttle without touching any other code
//  (and unit-test it on a host). It answers one question:
//
//      "For throttle value T (0..100), what servo pulse (in microseconds)
//       should we request?"
//
//  Ramp-up / ramp-down (time-based slew) is intentionally NOT here — that is
//  stateful and lives in ThrottleSupervisor. This module is the instantaneous
//  shape only.
// =============================================================================
#pragma once

#include <cstdint>

namespace motor {

// Response-curve family. `expo` (0..1) sets the intensity within a family.
enum class CurveMode : uint8_t {
    LINEAR      = 0,  // y = x                         (expo ignored)
    SOFT_START  = 1,  // y = x^(1+2*expo)  convex      (gentle, eases in from 0)
    PROGRESSIVE = 2,  // y = lerp(x, smoothstep(x), expo)  (S-curve build-up)
};

struct PowerCurveConfig {
    // --- input (throttle) side ---
    uint8_t deadband_pct = 3;   // throttle <= this => SAFE / neutral pulse

    // --- output (pulse) side, microseconds ---
    uint32_t neutral_us = 1000; // SAFE pulse (throttle 0 / failsafe). Match VESC!
    uint32_t out_min_us = 1000; // pulse at throttle just above deadband
    uint32_t out_max_us = 2000; // pulse at throttle = 100

    // --- response shaping ---
    CurveMode mode = CurveMode::LINEAR;
    float     expo = 0.0f;      // clamped to [0,1]
};

// Map throttle (0..100) to a pulse width in microseconds.
//   throttle <= deadband_pct           -> returns neutral_us (the SAFE state)
//   throttle in (deadband_pct .. 100]  -> shaped interpolation out_min..out_max
// Always clamped to the [out_min_us, out_max_us] band (order-independent).
uint32_t map_throttle_to_ppm_us(uint8_t throttle, const PowerCurveConfig& cfg);

// True when `pulse_us` equals the configured SAFE/neutral pulse.
inline bool is_safe_pulse(uint32_t pulse_us, const PowerCurveConfig& cfg)
{
    return pulse_us == cfg.neutral_us;
}

}  // namespace motor
