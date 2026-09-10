// =============================================================================
//  throttle_supervisor.cpp
// =============================================================================
#include "throttle_supervisor.hpp"

#include <algorithm>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace motor {

ThrottleSupervisor::ThrottleSupervisor(std::shared_ptr<IPpmOutput> output,
                                       PowerCurveConfig curve,
                                       SupervisorConfig cfg)
    : m_output(std::move(output)), m_curve(curve), m_cfg(cfg)
{
    // Startup is SAFE: no packet yet, not connected, output parked at neutral.
    m_current_us = m_curve.neutral_us;
}

void ThrottleSupervisor::on_connected()
{
    m_connected.store(true, std::memory_order_relaxed);
    ESP_LOGI(TAG, "BLE connected");
}

void ThrottleSupervisor::on_disconnected()
{
    m_connected.store(false, std::memory_order_relaxed);
    // Freshness is invalidated so the tick cannot treat old data as live.
    m_last_rx_us.store(0, std::memory_order_relaxed);
    m_last_logged_throttle = -1;
    ESP_LOGI(TAG, "BLE disconnected -> SAFE");
}

void ThrottleSupervisor::on_throttle(uint8_t throttle, uint16_t seq)
{
    if (throttle > 100) {
        throttle = 100;  // clamp out-of-range to a defined value
    }
    m_throttle.store(throttle, std::memory_order_relaxed);
    m_seq.store(seq, std::memory_order_relaxed);
    m_last_rx_us.store(esp_timer_get_time(), std::memory_order_relaxed);

    const bool changed = (static_cast<int>(throttle) != m_last_logged_throttle);
    if (m_cfg.log_every_packet || changed) {
        log_requested(throttle, seq);
        m_last_logged_throttle = throttle;
    }
}

void ThrottleSupervisor::log_requested(uint8_t throttle, uint16_t seq)
{
    // "RX throttle=XX seq=XXX" then the intended pulse. throttle<=deadband is
    // reported as SAFE (matches the requested bench trace).
    ESP_LOGI(TAG, "RX throttle=%u seq=%u", throttle, seq);

    const uint32_t target = map_throttle_to_ppm_us(throttle, m_curve);
    if (is_safe_pulse(target, m_curve)) {
        ESP_LOGI(TAG, "Requested PPM = SAFE");
    } else {
        ESP_LOGI(TAG, "Requested PPM = %u us (throttle=%u)", target, throttle);
    }
}

void ThrottleSupervisor::start()
{
    xTaskCreate(&ThrottleSupervisor::task_trampoline, "throttle_sup", 4096, this,
                6 /* high priority: failsafe must not be starved on 1-core C3 */,
                nullptr);
}

void ThrottleSupervisor::task_trampoline(void* arg)
{
    static_cast<ThrottleSupervisor*>(arg)->run();
}

void ThrottleSupervisor::run()
{
    const uint32_t tick_hz    = std::max<uint32_t>(1, m_cfg.tick_hz);
    const TickType_t period   = pdMS_TO_TICKS(1000 / tick_hz);
    const int64_t stale_us    = static_cast<int64_t>(m_cfg.stale_timeout_ms) * 1000;
    const uint32_t tick_ms    = 1000 / tick_hz;

    // Per-tick slew limits (us of pulse change allowed each tick).
    const uint32_t full_span  = (m_curve.out_max_us > m_curve.neutral_us)
                                  ? (m_curve.out_max_us - m_curve.neutral_us) : 1;
    const uint32_t step_up    = std::max<uint32_t>(
        1, full_span * tick_ms / std::max<uint32_t>(1, m_cfg.ramp_up_ms));
    const uint32_t step_down  = std::max<uint32_t>(
        1, full_span * tick_ms / std::max<uint32_t>(1, m_cfg.ramp_down_ms));

    // Enter SAFE before the loop.
    m_current_us = m_curve.neutral_us;
    m_output->go_safe();

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        const int64_t now       = esp_timer_get_time();
        const bool    connected = m_connected.load(std::memory_order_relaxed);
        const int64_t last_rx   = m_last_rx_us.load(std::memory_order_relaxed);
        const uint8_t throttle  = m_throttle.load(std::memory_order_relaxed);

        const bool have_data = (last_rx != 0);
        const bool stale     = (!have_data) || ((now - last_rx) > stale_us);
        const bool failsafe  = (!connected) || stale;

        uint32_t target;
        if (failsafe) {
            target = m_curve.neutral_us;
        } else {
            target = map_throttle_to_ppm_us(throttle, m_curve);
        }

        // Move current toward target.
        if (failsafe && m_cfg.failsafe_immediate) {
            m_current_us = m_curve.neutral_us;  // snap to SAFE
        } else if (target > m_current_us) {
            m_current_us = std::min(target, m_current_us + step_up);
        } else if (target < m_current_us) {
            const uint32_t dec = std::min(step_down, m_current_us - target);
            m_current_us -= dec;
        }

        m_output->set_pulse_us(m_current_us);

        vTaskDelayUntil(&last_wake, period);
    }
}

}  // namespace motor
