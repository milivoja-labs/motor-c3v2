// =============================================================================
//  config.hpp — Motor-side ESP32-C3 — all compile-time knobs in one place
// =============================================================================
//
//  SAFETY: This firmware controls the PPM/servo input of a Flipsky FSESC.
//  It ships BENCH-SAFE: MOTOR_OUTPUT_ENABLED is 0, so no physical output is
//  produced. It only *calculates* the intended PPM pulse and prints it over
//  USB serial. Do NOT set MOTOR_OUTPUT_ENABLED to 1 until a qualified
//  technician has:
//    1. Verified PPM_NEUTRAL_US matches the VESC's configured neutral in
//       VESC Tool (App Settings -> PPM), and
//    2. Bench-tested with the wheel off the ground and current limits set
//       conservatively in VESC Tool (this firmware does NOT set ESC power).
//
// =============================================================================
#pragma once

#include <cstdint>
#include "power_curve.hpp"
#include "throttle_supervisor.hpp"

// -----------------------------------------------------------------------------
//  MASTER SAFETY FLAG  ---  the "one compile-time flag" from the spec.
//  0 = bench-safe: calculate + log only, no physical output (DEFAULT).
//  1 = physical PPM output on PPM_OUTPUT_GPIO (technician sign-off required).
// -----------------------------------------------------------------------------
#ifndef MOTOR_OUTPUT_ENABLED
#define MOTOR_OUTPUT_ENABLED 0
#endif

namespace motor {
namespace cfg {

// --- Hardware ----------------------------------------------------------------
// GPIO that carries the PPM signal to the FSESC PPM connector (signal wire).
//
// ⚠  SET THIS TO THE SAME GPIO YOUR OLD FIRMWARE USED for the ESC signal wire.
//    The physical wire to the FSESC PPM connector doesn't change — only the
//    signal on it does. I could not read the pin number out of your compiled
//    motor.bin, so confirm it against your old source/wiring and set it here.
//    (GPIO6 is only a placeholder default.) Only used when output is enabled.
constexpr int PPM_OUTPUT_GPIO = 6;

// --- BLE identity  (MUST MATCH your ESP32-S3 throttle firmware) --------------
// The central connects to a device advertising EITHER this name OR the service
// UUID in throttle_protocol.hpp. Set at least one to match your S3.
constexpr const char* TARGET_NAME = "HopUp-Throttle";

// --- PPM pulse endpoints (microseconds) --------------------------------------
// Matched to your VESC Tool PPM config:
//     Start = 1.000 ms   Center = 1.500 ms   End = 2.000 ms
// giving the exact linear mapping you specified:
//     0% throttle -> 1000 us,  50% -> 1500 us,  100% -> 2000 us   (~50 Hz frame)
//
//   PPM_NEUTRAL_US : the SAFE/failsafe state. Sent at boot, at throttle=0, and
//                    on every failsafe (disconnect / stale / startup). You asked
//                    failsafe -> 1000 us, so this is 1000 (= VESC Start).
//                    NOTE: this is only "no motion" if your VESC is a forward-
//                    only mode where Start(1000us)=zero output (e.g. Current No
//                    Reverse Brake). If you ever switch to a bidirectional mode
//                    where Center(1500us)=neutral, change this to 1500.
//   PPM_MIN_US     : pulse at throttle 0%   (= Start).
//   PPM_MAX_US     : pulse at throttle 100% (= End).
constexpr uint32_t PPM_NEUTRAL_US = 1000;
constexpr uint32_t PPM_MIN_US     = 1000;
constexpr uint32_t PPM_MAX_US     = 2000;

// --- Power curve (tunable mapping; see power_curve.hpp) -----------------------
// Deadband 0 + LINEAR gives the exact 1000/1500/2000 mapping above. (The curve
// module still supports deadband / SOFT_START / PROGRESSIVE / expo if you want
// to shape response later — changing these only changes feel, never the BLE or
// output code.)
constexpr uint8_t   THROTTLE_DEADBAND_PCT = 0;                 // 0 => clean linear map
constexpr CurveMode CURVE_MODE            = CurveMode::LINEAR; // LINEAR/SOFT_START/PROGRESSIVE
constexpr float     CURVE_EXPO            = 0.0f;              // 0..1 curve intensity

// --- Supervisor timing / failsafe --------------------------------------------
constexpr uint32_t TICK_HZ            = 50;   // output evaluation rate (matches 50 Hz frame)
constexpr uint32_t STALE_TIMEOUT_MS  = 300;  // no fresh packet within this => failsafe
constexpr uint32_t RAMP_UP_MS        = 400;  // neutral->full slew (normal accel)
constexpr uint32_t RAMP_DOWN_MS      = 250;  // full->neutral slew (normal throttle release)
constexpr bool     FAILSAFE_IMMEDIATE = true; // disconnect/stale => snap to neutral (safest)

// --- Logging -----------------------------------------------------------------
// Log every received packet (true) vs only when the throttle value changes
// (false, default — matches the clean bench trace in the spec).
constexpr bool LOG_EVERY_PACKET = false;

// -----------------------------------------------------------------------------
//  Factory helpers — build the config structs from the constants above.
// -----------------------------------------------------------------------------
inline PowerCurveConfig make_curve()
{
    PowerCurveConfig c;
    c.deadband_pct = THROTTLE_DEADBAND_PCT;
    c.neutral_us   = PPM_NEUTRAL_US;
    c.out_min_us   = PPM_MIN_US;
    c.out_max_us   = PPM_MAX_US;
    c.mode         = CURVE_MODE;
    c.expo         = CURVE_EXPO;
    return c;
}

inline SupervisorConfig make_supervisor()
{
    SupervisorConfig s;
    s.tick_hz            = TICK_HZ;
    s.stale_timeout_ms   = STALE_TIMEOUT_MS;
    s.ramp_up_ms         = RAMP_UP_MS;
    s.ramp_down_ms       = RAMP_DOWN_MS;
    s.failsafe_immediate = FAILSAFE_IMMEDIATE;
    s.log_every_packet   = LOG_EVERY_PACKET;
    return s;
}

}  // namespace cfg
}  // namespace motor
