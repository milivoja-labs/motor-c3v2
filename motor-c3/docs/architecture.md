# Motor-side C3 — architecture & the PWM→PPM fix

## The bug in one paragraph

The old firmware produced a **20 kHz duty-cycle PWM** (50 µs period, command =
duty ratio). A VESC PPM/servo input measures a **pulse width** of roughly
1000–2000 µs delivered at **~50 Hz** (20 ms period). A 50 µs frame is 20× too
short to contain even a 1 ms pulse, so the VESC's PPM capture finds no valid
pulse and VESC Tool reads **0.0000 ms**. It is a signal-format mismatch, not a
calibration issue: duty-ratio-at-20 kHz and pulse-width-at-50 Hz are different
representations. The fix is to emit a real servo waveform — a ~50 Hz frame whose
HIGH time is the 1000–2000 µs command pulse.

## C3 constraint

The ESP32-C3 has **no MCPWM** peripheral (unlike the S3). Servo pulses therefore
come from **LEDC reconfigured to 50 Hz** (used here) or the **RMT** peripheral.
LEDC at 50 Hz with 14-bit duty (the C3 max) gives ~1.22 µs resolution across the
20 ms frame — ample for a servo pulse.

## Layers

```
 NimBLE central          ThrottleSupervisor         PowerCurve            IPpmOutput
 (ble_central.cpp)       (throttle_supervisor.*)    (power_curve.*)       (ppm_output.hpp)
 ─────────────────       ──────────────────────     ──────────────       ─────────────────
 scan / connect /   ->   safety authority:      ->  pure map:        ->  NullPpmOutput  (default: log only)
 bond / subscribe        startup-SAFE, throttle     deadband, min/max,    LedcPpmOutput  (gated: 50 Hz pulse)
 -> (throttle, seq)      =0 -> SAFE, stale -> SAFE,  expo, LINEAR /
                         disconnect -> SAFE, ramp    SOFT_START /
                                                     PROGRESSIVE
```

Each layer has a single job and no knowledge of the others' internals:

- **ble_central** speaks only BLE + the throttle protocol. Change UUIDs/payload
  in `throttle_protocol.hpp`; nothing else moves.
- **power_curve** is pure (no BLE/FreeRTOS/IDF). Change the feel of the throttle
  here without touching BLE or output code. Host-testable.
- **throttle_supervisor** is the only place that decides the live value and
  enforces every failsafe. It ramps normal changes and snaps to SAFE on any
  fault.
- **IPpmOutput** hides "do we actually drive a pin". `MOTOR_OUTPUT_ENABLED`
  selects Null (bench) vs Ledc (live) in `main.cpp` — the safety logic above is
  identical in both cases, so the bench trace is faithful to live behaviour.

## Failsafe truth table (what pulse the output holds)

| Condition                              | Output |
|----------------------------------------|--------|
| Startup, before any packet             | SAFE (neutral) |
| BLE disconnected                       | SAFE (immediate) |
| No fresh packet within stale timeout   | SAFE |
| throttle ≤ deadband                    | SAFE (neutral) |
| Fresh packet, throttle > deadband      | mapped pulse, ramp-limited |

"SAFE" = `PPM_NEUTRAL_US`. Normal accel/decel obey `ramp_up_ms`/`ramp_down_ms`;
faults bypass the ramp and snap to neutral (`failsafe_immediate`, default on).

## Enabling physical output (later)

One flag, after technician review (see `docs/ppm-config.md`):

```
idf.py -DMOTOR_OUTPUT_ENABLED=1 build flash
```

This swaps `NullPpmOutput` for `LedcPpmOutput` and compiles in
`ledc_ppm_output.cpp`. Nothing else changes.
