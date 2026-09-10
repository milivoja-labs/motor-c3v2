# motor (wheel-side) C3 — PPM firmware flasher

Self-contained flasher for the motor-side ESP32-C3, same style as your previous
package. You do **not** need ESP-IDF, Python, or any Espressif tooling.

This build outputs **VESC-compatible PPM/servo pulses** (replacing the old
20 kHz PWM that read `0.0000 ms`), matched to your VESC config:
`0% = 1000 µs, 50% = 1500 µs, 100% = 2000 µs, ~50 Hz`. Failsafe (BLE
disconnect/timeout/startup) = **1000 µs**. Physical output is **disabled by
default** for bench verification — it logs the received throttle and calculated
PPM over serial without driving the pin.

## Flash it

1. Connect only this one unit over USB.
2. In this folder:  `.\flash.ps1`   (Windows)  or  `./flash.sh` (Linux/macOS if present)
3. Wait for `Done.`

If Windows blocks the script:
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\flash.ps1
```

Watch the serial console after flashing:
```powershell
.\flash.ps1 -Monitor
```
You should see `RX throttle=…` and `Requested PPM = … us` lines.

## Options

| What | Windows |
| --- | --- |
| Pick the port | `.\flash.ps1 -Port COM9` |
| Flash faster | `.\flash.ps1 -Baud 460800` |
| Keep pairing (no erase) | `.\flash.ps1 -NoErase` |
| Just watch the console | `.\flash.ps1 -MonitorOnly` |

Full erase (the default) wipes the BLE bond, so re-pair the throttle and motor
units afterward — flash both, then power them up together.

## Enabling physical output (later, with sign-off)

This package is bench-safe. To get a package that actually drives the ESC PPM
pin, re-run the cloud build with the output flag enabled (see the project's
`.github/workflows/build.yml` → "Run workflow" → set enable_output = 1), after a
technician has verified VESC neutral and current limits. This firmware never
changes VESC motor/current settings.
