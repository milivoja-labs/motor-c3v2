// =============================================================================
//  main.cpp — composition root for the motor-side ESP32-C3
// =============================================================================
//  The ONLY place that instantiates concrete classes. It picks the output
//  implementation from MOTOR_OUTPUT_ENABLED, builds the curve + supervisor
//  configs from config.hpp, wires them together, and starts BLE.
//
//  Bench-safe by default: with MOTOR_OUTPUT_ENABLED == 0 you get a NullPpmOutput
//  and no pin is ever driven — only calculated values are logged over serial.
// =============================================================================
#include <memory>

#include "config.hpp"
#include "ble_central.hpp"
#include "null_ppm_output.hpp"
#include "power_curve.hpp"
#include "ppm_output.hpp"
#include "throttle_supervisor.hpp"

#if MOTOR_OUTPUT_ENABLED
#include "ledc_ppm_output.hpp"
#endif

#include "esp_log.h"

namespace {
constexpr const char* TAG = "MotorMain";
}

extern "C" void app_main()
{
    using namespace motor;

    ESP_LOGI(TAG, "==== Motor-side ESP32-C3 (PPM / servo output) ====");

    // --- select the output (this is the whole effect of the safety flag) ---
    std::shared_ptr<IPpmOutput> output;
#if MOTOR_OUTPUT_ENABLED
    ESP_LOGW(TAG, "*** MOTOR_OUTPUT_ENABLED=1 — PHYSICAL PPM WILL BE DRIVEN ***");
    ESP_LOGW(TAG, "*** Confirm VESC neutral + current limits before power.   ***");
    output = std::make_shared<LedcPpmOutput>(cfg::PPM_OUTPUT_GPIO);
#else
    ESP_LOGI(TAG, "MOTOR_OUTPUT_ENABLED=0 — bench-safe: calculate + log only.");
    output = std::make_shared<NullPpmOutput>();
#endif

    // --- build configs from the single source of truth (config.hpp) ---
    const PowerCurveConfig curve = cfg::make_curve();
    const SupervisorConfig scfg  = cfg::make_supervisor();

    ESP_LOGI(TAG, "Curve: mode=%d expo=%.2f deadband=%u%% out=[%u..%u]us neutral=%u us",
             static_cast<int>(curve.mode), curve.expo, curve.deadband_pct,
             curve.out_min_us, curve.out_max_us, curve.neutral_us);
    ESP_LOGI(TAG, "Failsafe: stale>%ums -> SAFE, disconnect -> SAFE, startup SAFE",
             scfg.stale_timeout_ms);

    // --- park output at SAFE before anything can request motion ---
    if (!output->init(curve.neutral_us)) {
        ESP_LOGE(TAG, "output init failed — staying halted");
        return;
    }

    // --- supervisor (safety authority) + its fixed-rate task ---
    auto supervisor = std::make_shared<ThrottleSupervisor>(output, curve, scfg);
    supervisor->start();

    // --- BLE central: find the S3, subscribe, feed the supervisor ---
    BleCentral::instance().start(supervisor);

    ESP_LOGI(TAG, "Running. Waiting for BLE throttle link...");
}
