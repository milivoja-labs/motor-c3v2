# VESC PPM setup & the neutral-pulse safety check

Read this before setting `MOTOR_OUTPUT_ENABLED=1`.

## The one value you must get right: neutral

`PPM_NEUTRAL_US` (config.hpp) is the pulse sent at boot, at throttle 0, and on
every failsafe. It **must** match how your FSESC interprets neutral, or the very
first thing the firmware does on power-up is command motion.

- **Forward-only throttle (typical e-bike):** the VESC is mapped so the minimum
  pulse = zero output. Then neutral = minimum = **1000 µs** (the default here).
- **Centered / bidirectional config (reverse below center):** neutral =
  **1500 µs**. If you leave `PPM_NEUTRAL_US` at 1000 with a centered VESC, boot
  commands **full reverse**. Set it to 1500.

Bench-safe mode is exactly where you catch this: at throttle 0 the serial log
prints `Requested PPM = SAFE`, and the startup log prints the neutral value.
Confirm that number is your VESC's true neutral before enabling output.

## Suggested VESC Tool procedure (done by/with a technician)

1. Wheel **off the ground**. Set conservative **motor** and **battery current
   limits** in VESC Tool. This firmware never changes ESC power settings — they
   live entirely in VESC Tool.
2. App Settings → **PPM**. Choose the control mode you want (e.g. *Current No
   Reverse* for a forward-only throttle).
3. Use **Display / PPM** live readout while bench-testing this firmware with
   output enabled: verify that throttle 0 shows your neutral pulse and throttle
   100 shows ~2000 µs, matching the serial `Requested PPM = …` lines.
4. Run **Calibrate** / set pulse start/center/end in VESC Tool to match the
   `PPM_MIN_US` / `PPM_NEUTRAL_US` / `PPM_MAX_US` you configured.
5. Only after the pulse readout tracks the serial log across 0→25→50→100 and
   back, and returns to neutral on disconnect, consider a low-current spin test.

## Wiring

- C3 `PPM_OUTPUT_GPIO` (default GPIO6) → FSESC PPM **signal**.
- Common **ground** between the C3 and the FSESC.
- Do **not** back-power the C3 from the FSESC 5 V unless you have confirmed it is
  safe for your board; power the C3 from USB during bench testing.

## Reminder

Physical output stays **disabled by default**. The enable step is a deliberate,
reviewed action — not something to flip on to "just try it".
