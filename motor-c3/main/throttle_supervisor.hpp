// =============================================================================
//  throttle_supervisor.hpp — the safety authority
// =============================================================================
//  Owns the decision "what pulse should the output hold, right now?" and
//  enforces every safety rule:
//    - startup begins SAFE (neutral), before any packet arrives
//    - throttle == 0 (<= deadband) maps to SAFE
//    - BLE disconnect -> SAFE immediately
//    - no fresh packet within stale_timeout_ms -> SAFE
//    - normal changes are ramp/slew limited (ramp_up_ms / ramp_down_ms)
//
//  It runs a fixed-rate task (tick_hz) that evaluates state and pushes the
//  value to the IPpmOutput. Receiving throttle updates and connect/disconnect
//  events happens from the BLE task via the on_*() methods (lock-free atomics).
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "power_curve.hpp"
#include "ppm_output.hpp"

namespace motor {

struct SupervisorConfig {
    uint32_t tick_hz            = 50;
    uint32_t stale_timeout_ms  = 300;
    uint32_t ramp_up_ms        = 400;
    uint32_t ramp_down_ms      = 250;
    bool     failsafe_immediate = true;  // disconnect/stale => snap to neutral
    bool     log_every_packet   = false; // else log only on throttle change
};

class ThrottleSupervisor {
public:
    ThrottleSupervisor(std::shared_ptr<IPpmOutput> output,
                       PowerCurveConfig curve,
                       SupervisorConfig cfg);

    // --- called from the BLE task ---
    void on_connected();
    void on_disconnected();
    void on_throttle(uint8_t throttle, uint16_t seq);

    // Start the fixed-rate evaluation task.
    void start();

    ThrottleSupervisor(const ThrottleSupervisor&)            = delete;
    ThrottleSupervisor& operator=(const ThrottleSupervisor&) = delete;

private:
    static void task_trampoline(void* arg);
    void run();
    void log_requested(uint8_t throttle, uint16_t seq);

    static constexpr const char* TAG = "ThrottleSupervisor";

    std::shared_ptr<IPpmOutput> m_output;
    PowerCurveConfig            m_curve;
    SupervisorConfig            m_cfg;

    // Shared with the BLE task.
    std::atomic<uint8_t>  m_throttle{0};
    std::atomic<uint16_t> m_seq{0};
    std::atomic<int64_t>  m_last_rx_us{0};
    std::atomic<bool>     m_connected{false};

    // Task-local (only touched inside run()).
    uint32_t m_current_us = 0;

    // Logging de-dupe (only touched inside on_throttle()).
    int m_last_logged_throttle = -1;
};

}  // namespace motor
