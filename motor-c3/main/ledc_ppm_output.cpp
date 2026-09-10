// =============================================================================
//  ledc_ppm_output.cpp — 50 Hz servo pulse on LEDC (compiled only when gated)
// =============================================================================
#include "ledc_ppm_output.hpp"

#include <algorithm>

#include "driver/ledc.h"
#include "esp_log.h"

namespace motor {

namespace {
constexpr ledc_timer_t   TIMER   = LEDC_TIMER_0;
constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_0;
// The ESP32-C3 only has the low-speed LEDC speed mode.
constexpr ledc_mode_t    MODE    = LEDC_LOW_SPEED_MODE;
}  // namespace

uint32_t LedcPpmOutput::us_to_duty(uint32_t pulse_us) const
{
    // Clamp the pulse to a sane servo window before converting.
    pulse_us = std::min<uint32_t>(pulse_us, FRAME_US);  // never wider than the frame
    // counts = pulse_us / FRAME_US * DUTY_MAX, rounded.
    const uint64_t counts =
        (static_cast<uint64_t>(pulse_us) * DUTY_MAX + FRAME_US / 2) / FRAME_US;
    return static_cast<uint32_t>(std::min<uint64_t>(counts, DUTY_MAX - 1));
}

bool LedcPpmOutput::init(uint32_t neutral_us)
{
    m_neutral_us = neutral_us;

    ledc_timer_config_t tcfg = {};
    tcfg.speed_mode      = MODE;
    tcfg.duty_resolution = static_cast<ledc_timer_bit_t>(DUTY_BITS);
    tcfg.timer_num       = TIMER;
    tcfg.freq_hz         = FRAME_HZ;
    tcfg.clk_cfg         = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s (freq/res may be unsupported)",
                 esp_err_to_name(err));
        return false;
    }

    ledc_channel_config_t ccfg = {};
    ccfg.gpio_num   = m_gpio;
    ccfg.speed_mode = MODE;
    ccfg.channel    = CHANNEL;
    ccfg.timer_sel  = TIMER;
    ccfg.duty       = us_to_duty(m_neutral_us);  // park at SAFE before signal is live
    ccfg.hpoint     = 0;
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return false;
    }

    m_ready = true;
    ESP_LOGW(TAG,
             "PHYSICAL PPM ready: %u Hz, %d-bit, GPIO %d, parked at SAFE (%u us). "
             "Verify neutral in VESC Tool before applying power.",
             FRAME_HZ, DUTY_BITS, m_gpio, m_neutral_us);
    return true;
}

void LedcPpmOutput::set_pulse_us(uint32_t pulse_us)
{
    if (!m_ready) {
        return;
    }
    const uint32_t duty = us_to_duty(pulse_us);
    ledc_set_duty(MODE, CHANNEL, duty);
    ledc_update_duty(MODE, CHANNEL);
}

void LedcPpmOutput::go_safe()
{
    set_pulse_us(m_neutral_us);
}

}  // namespace motor
