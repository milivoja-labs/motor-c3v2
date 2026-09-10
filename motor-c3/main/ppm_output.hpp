// =============================================================================
//  ppm_output.hpp — abstraction over "emit a servo pulse of N microseconds"
// =============================================================================
//  Two implementations exist:
//    NullPpmOutput  — bench-safe. Computes/holds the value but drives NO pin.
//    LedcPpmOutput  — real 50 Hz servo pulse via LEDC (gated by the flag).
//
//  ThrottleSupervisor talks only to this interface, so switching between
//  bench and live is a one-line change in the composition root (main.cpp),
//  driven by MOTOR_OUTPUT_ENABLED. The safety logic is identical either way.
// =============================================================================
#pragma once

#include <cstdint>

namespace motor {

class IPpmOutput {
public:
    virtual ~IPpmOutput() = default;

    // Prepare the output and park it at the SAFE/neutral pulse. Returns false
    // on hardware init failure (bench/Null always returns true).
    virtual bool init(uint32_t neutral_us) = 0;

    // Request a servo pulse of `pulse_us` microseconds. Idempotent.
    virtual void set_pulse_us(uint32_t pulse_us) = 0;

    // Force the SAFE/neutral pulse immediately.
    virtual void go_safe() = 0;

    // True if this implementation drives a physical pin.
    virtual bool is_physical() const = 0;

    // Non-copyable, non-movable (owns hardware / is shared via shared_ptr).
    IPpmOutput() = default;
    IPpmOutput(const IPpmOutput&)            = delete;
    IPpmOutput& operator=(const IPpmOutput&) = delete;
    IPpmOutput(IPpmOutput&&)                 = delete;
    IPpmOutput& operator=(IPpmOutput&&)      = delete;
};

}  // namespace motor
