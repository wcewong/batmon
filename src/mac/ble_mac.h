#pragma once
// macOS Bluetooth / Apple battery sources, complementing the HID drivers:
//   * IORegistry "BatteryPercent" — Apple Magic Mouse/Trackpad/Keyboard and anything the system
//     already annotates with a battery level.
//   * CoreBluetooth standard Battery Service (0x180F / 0x2A19) — standard BLE devices the system is
//     connected to (e.g. BLE keyboards, knobs) that do not go through a vendor HID driver.
#include <string>
#include <vector>

struct MacBattery {
    std::wstring name;
    std::wstring address;   // Bluetooth address or registry id, for stable keys / dedup
    std::wstring via;       // "Bluetooth"
    int percent = -1;
    int kind = -1;          // DeviceKind value from the BT minor type; -1 = unknown (guess by name)
};

// One blocking sweep of both sources. CoreBluetooth reads are bounded by `timeoutMs`. Safe to call
// from a worker thread.
std::vector<MacBattery> MacPollBluetoothBatteries(int timeoutMs);
