#pragma once
// Bluetooth device identity is its Windows device-container ID, not its display name.
// Keep the legacy name-based fallback only for devices reported by other drivers
// (e.g. vendor drivers), while allowing two Bluetooth controllers with identical names.
#include "../devices.h"

#include <vector>

inline bool IsDuplicateBluetoothDevice(const DeviceStatus& candidate, const std::vector<DeviceStatus>& existing) {
    for (const auto& other : existing) {
        if (other.key == candidate.key) return true;
        // Vendor drivers (AirPods, Galaxy Buds, Rapoo) also say via "Bluetooth", so tell
        // PollBluetooth's own entries apart by their "bt:" key instead.
        if (other.key.rfind(L"bt:", 0) != 0 && other.name == candidate.name) return true;
    }
    return false;
}
