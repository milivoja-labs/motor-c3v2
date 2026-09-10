// =============================================================================
//  throttle_protocol.hpp — Shared BLE contract with the ESP32-S3 throttle unit
// =============================================================================
//
//  ⚠  THIS IS THE ONE FILE YOU MUST RECONCILE WITH YOUR S3 FIRMWARE.  ⚠
//
//  The S3 (peripheral) advertises a service and notifies a characteristic that
//  carries the throttle value. For the C3 central to find, connect and
//  subscribe, the UUIDs and payload layout below must be IDENTICAL to the S3.
//  If your S3 uses different UUIDs or a different packet layout, change them
//  HERE only — no other file needs to change. Nothing about the S3 side is
//  modified by this project.
//
//  NimBLE note: BLE_UUID128_INIT takes the 16 bytes LEAST-significant first
//  (i.e. the reverse of how the UUID string is printed). The comment above each
//  UUID shows the human-readable form.
// =============================================================================
#pragma once

#include <cstdint>
#include "host/ble_uuid.h"

namespace motor {
namespace proto {

// Service UUID  =  6e40aa10-b5a3-f393-e0a9-e50e24dcca9e   (EXAMPLE — replace)
static const ble_uuid128_t THROTTLE_SVC_UUID = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x10, 0xaa, 0x40, 0x6e);

// Characteristic UUID = 6e40aa11-b5a3-f393-e0a9-e50e24dcca9e (EXAMPLE — replace)
static const ble_uuid128_t THROTTLE_CHR_UUID = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x11, 0xaa, 0x40, 0x6e);

// -----------------------------------------------------------------------------
//  Notification payload.
//  Matches the existing RX log "RX throttle=XX seq=XXX": one throttle byte
//  (0..100) plus a rolling 16-bit sequence number. If your S3 packs these
//  differently (endianness, extra fields, a header/version byte), mirror it
//  here. Keep it packed so the byte layout is deterministic on both sides.
// -----------------------------------------------------------------------------
struct __attribute__((packed)) ThrottleMsg {
    uint8_t  throttle;  // 0..100
    uint16_t seq;       // little-endian rolling sequence
};

static constexpr uint8_t THROTTLE_MAX = 100;

// Best-effort parse from a raw notification buffer. Returns false if the buffer
// is too short or the throttle value is out of range (treated as unsafe -> the
// supervisor will hold SAFE).
inline bool parse(const uint8_t* data, uint16_t len, ThrottleMsg& out)
{
    if (data == nullptr || len < sizeof(ThrottleMsg)) {
        return false;
    }
    out.throttle = data[0];
    out.seq      = static_cast<uint16_t>(data[1] | (data[2] << 8));
    return out.throttle <= THROTTLE_MAX;
}

}  // namespace proto
}  // namespace motor
