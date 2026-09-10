// =============================================================================
//  ble_central.hpp — NimBLE central: find the S3 throttle, subscribe, forward
// =============================================================================
//  Responsibilities:
//    - scan for the throttle peripheral (by name and/or service UUID)
//    - connect, initiate pairing/bonding (encrypted link)
//    - discover the throttle service + characteristic, enable notifications
//    - on each notification, parse and hand (throttle, seq) to the supervisor
//    - on connect/disconnect, notify the supervisor
//
//  It deliberately knows nothing about PPM, curves or output — it only speaks
//  BLE and the throttle protocol. Swapping the mapping or output never touches
//  this file (and vice-versa).
// =============================================================================
#pragma once

#include <memory>

#include "throttle_supervisor.hpp"

namespace motor {

class BleCentral {
public:
    static BleCentral& instance();

    // Wire the supervisor and start the NimBLE host + scanning.
    void start(std::shared_ptr<ThrottleSupervisor> supervisor);

    // --- internal (called from NimBLE C callbacks via the singleton) ---
    ThrottleSupervisor* supervisor() { return m_supervisor.get(); }

private:
    BleCentral() = default;
    std::shared_ptr<ThrottleSupervisor> m_supervisor;
};

}  // namespace motor
