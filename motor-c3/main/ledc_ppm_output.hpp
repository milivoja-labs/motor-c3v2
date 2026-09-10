// =============================================================================
//  ledc_ppm_output.hpp — PHYSICAL 50 Hz servo/PPM output via LEDC (GATED)
// =============================================================================
//
//  ⚠  This drives a real signal into the FSESC PPM input. It is only compiled
//     and only instantiated when MOTOR_OUTPUT_ENABLED == 1. Do not enable
//     without technician sign-off (see config.hpp / docs/ppm-config.md).
//
//  Why LEDC and not MCPWM: the ESP32-C3 has no MCPWM peripheral. LEDC can be
//  clocked down to 50 Hz, which gives a standard 20 ms servo frame; the HIGH
//  time (the duty) becomes the 1000-2000 us pulse the VESC decodes.
// =============================================================================
#pragma once

#include "ppm_output.hpp"

namespace motor {

class LedcPpmOutput final : public IPpmOutput {
public:
    explicit LedcPpmOutput(int gpio) : m_gpio(gpio) {}

    bool init(uint32_t neutral_us) override;
    void set_pulse_us(uint32_t pulse_us) override;
    void go_safe() override;
    bool is_physical() const override { return true; }

private:
    uint32_t us_to_duty(uint32_t pulse_us) const;

    static constexpr const char* TAG = "LedcPpm";

    // 50 Hz frame (20 ms). 14-bit is the ESP32-C3 LEDC max duty resolution;
    // at 50 Hz that is 2^14 counts across 20 ms = ~1.22 us per count.
    static constexpr uint32_t FRAME_HZ    = 50;
    static constexpr uint32_t FRAME_US    = 1000000UL / FRAME_HZ;  // 20000
    static constexpr int      DUTY_BITS   = 14;
    static constexpr uint32_t DUTY_MAX    = (1UL << DUTY_BITS);    // 16384

    int      m_gpio;
    uint32_t m_neutral_us = 1000;
    bool     m_ready      = false;
};

}  // namespace motor
