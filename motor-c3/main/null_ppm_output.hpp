// =============================================================================
//  null_ppm_output.hpp — BENCH-SAFE output: no physical pin is ever driven
// =============================================================================
//  This is the DEFAULT output (MOTOR_OUTPUT_ENABLED == 0). It records the
//  requested pulse so the rest of the system behaves identically to the live
//  path, but it never configures or toggles a GPIO. All human-visible logging
//  of requested values happens in ThrottleSupervisor; this class only emits a
//  low-noise DEBUG line so INFO-level bench traces stay clean.
// =============================================================================
#pragma once

#include "ppm_output.hpp"
#include "esp_log.h"
#include <inttypes.h>

namespace motor {

class NullPpmOutput final : public IPpmOutput {
public:
    bool init(uint32_t neutral_us) override
    {
        m_neutral_us = neutral_us;
        m_current_us = neutral_us;
        ESP_LOGI(TAG, "Bench-safe output active — NO physical pin driven (neutral=%" PRIu32 " us)",
                 neutral_us);
        return true;
    }

    void set_pulse_us(uint32_t pulse_us) override
    {
        m_current_us = pulse_us;
        ESP_LOGD(TAG, "[SIM] would emit %" PRIu32 " us", pulse_us);
    }

    void go_safe() override
    {
        m_current_us = m_neutral_us;
        ESP_LOGD(TAG, "[SIM] SAFE (%" PRIu32 " us)", m_neutral_us);
    }

    bool is_physical() const override { return false; }

    uint32_t current_us() const { return m_current_us; }

private:
    static constexpr const char* TAG = "NullPpm";
    uint32_t m_neutral_us = 1000;
    uint32_t m_current_us = 1000;
};

}  // namespace motor
