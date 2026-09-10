# motor-c3 — Motor-side ESP32-C3 firmware (bench-safe PPM)

BLE-central firmware for the wheel-side ESP32-C3. It receives the 0–100 throttle
from the ESP32-S3 over BLE and computes the **PPM/servo pulse** the Flipsky FSESC
expects — replacing the old 20 kHz duty-cycle PWM that VESC Tool read as
`0.0000 ms`. See `docs/architecture.md` for the why.

**Ships bench-safe:** `MOTOR_OUTPUT_ENABLED = 0`. No pin is driven; the firmware
only calculates and prints the intended pulse over USB serial so you can verify
received throttle and computed output before anything can move.

## Layout

```
motor-c3/
├── CMakeLists.txt              project
├── sdkconfig.defaults          C3 + NimBLE central + bonding + large app
├── flash.ps1                   Windows build / flash / monitor / erase
├── docs/
│   ├── architecture.md         PWM→PPM explanation + layer design
│   └── ppm-config.md           VESC neutral safety check + wiring
└── main/
    ├── config.hpp              ALL tunables + the MOTOR_OUTPUT_ENABLED flag
    ├── throttle_protocol.hpp   BLE UUIDs + packet — MUST MATCH your S3
    ├── power_curve.hpp/.cpp    pure throttle→pulse map (deadband/min/max/expo/modes)
    ├── ppm_output.hpp          output interface
    ├── null_ppm_output.hpp     bench output (no pin) — default
    ├── ledc_ppm_output.hpp/.cpp physical 50 Hz servo pulse — gated
    ├── throttle_supervisor.hpp/.cpp  safety authority + ramp + failsafe
    ├── ble_central.hpp/.cpp    NimBLE central: scan/connect/bond/subscribe
    └── main.cpp                composition root
```

## Before first build: match your S3

Only one file needs reconciling with your existing throttle firmware —
`main/throttle_protocol.hpp`:

- Set `THROTTLE_SVC_UUID` / `THROTTLE_CHR_UUID` to the UUIDs your S3 advertises
  and notifies (the values in the file are examples).
- Set `cfg::TARGET_NAME` in `config.hpp` to the S3's advertised name (or rely on
  the service-UUID match).
- Confirm the `ThrottleMsg` byte layout matches what the S3 sends (throttle byte
  + 16-bit seq by default).

Nothing about the S3 firmware is changed by this project.

## Build & flash (Windows PowerShell)

From an **ESP-IDF PowerShell** (one where `idf.py` is on PATH):

```powershell
# One-time per session if the script is blocked:
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

# Bench-safe build + flash + monitor (auto-detects the COM port):
.\flash.ps1

# Monitor only (no build/flash):
.\flash.ps1 -Action monitor -Port COM5

# Just build:
.\flash.ps1 -Action build

# Erase flash incl. BLE bonds (fresh pairing):
.\flash.ps1 -Action erase -Port COM5
```

Find the port by plugging in the board and running
`[System.IO.Ports.SerialPort]::GetPortNames()`.

If your C3 uses the native USB (USB-Serial-JTAG) rather than a CH340/CP210x
bridge, see the console note in `sdkconfig.defaults`.

## Expected bench serial trace

```
I (…) MotorMain: MOTOR_OUTPUT_ENABLED=0 — bench-safe: calculate + log only.
I (…) BleCentral: Scanning for 'HopUp-Throttle'...
I (…) ThrottleSupervisor: BLE connected
I (…) ThrottleSupervisor: RX throttle=0 seq=1
I (…) ThrottleSupervisor: Requested PPM = SAFE
I (…) ThrottleSupervisor: RX throttle=25 seq=42
I (…) ThrottleSupervisor: Requested PPM = 1250 us (throttle=25)
I (…) ThrottleSupervisor: RX throttle=50 seq=88
I (…) ThrottleSupervisor: Requested PPM = 1500 us (throttle=50)
I (…) ThrottleSupervisor: RX throttle=100 seq=150
I (…) ThrottleSupervisor: Requested PPM = 2000 us (throttle=100)
I (…) ThrottleSupervisor: BLE disconnected -> SAFE
```

(The `I (timestamp) TAG:` prefix is standard ESP-IDF logging. Mapping matches
your VESC config exactly: 0%→1000 µs, 50%→1500 µs, 100%→2000 µs, failsafe→1000 µs.)

## Enabling physical output — later, with sign-off

Do **not** do this until a technician has verified VESC neutral and current
limits (`docs/ppm-config.md`):

```powershell
.\flash.ps1 -Action flash -Port COM5 -EnableOutput   # prompts for confirmation
```

This is the single switch: it builds with `MOTOR_OUTPUT_ENABLED=1`, swapping the
Null output for the real 50 Hz LEDC servo pulse on `PPM_OUTPUT_GPIO`. All safety
logic is unchanged.

## Tuning the power curve

Everything is in `main/config.hpp` (deadband, min/max pulse, ramp up/down, expo,
and `CURVE_MODE` = LINEAR / SOFT_START / PROGRESSIVE). The mapping itself lives
in `main/power_curve.cpp` as a pure function, so you can change the feel without
touching BLE or output code. This firmware does not set motor/battery current or
any ESC power parameter — those stay in VESC Tool.
