#pragma once
// Windows Apple Accessory Protocol (AAP) session manager for AirPods / Beats.
// Talks to an installed kernel L2CAP bridge driver (NTPodsAAP, PodPilotAap, or MagicAAP)
// over standard Win32 device I/O to obtain true 1%-granular battery telemetry.
#include <windows.h>

#include <cstdint>
#include <string>

#include "airpods_protocol.h"

namespace airpods_aap {

struct Snapshot {
    AirPodsAap::BatteryReading battery;
    ULONGLONG updatedTick = 0;
    bool connected = false;
    std::wstring backend;
};

// Ensures a background AAP session is running for a connected Apple BT device (48-bit btAddr).
// Returns true if a new session was just started (so the caller can wait briefly for the initial packet).
bool EnsureSession(UINT64 btAddr);

// Returns true while a session is currently opening or waiting for its initial battery reading.
bool IsStarting(UINT64 btAddr);

// Closes any active AAP session for a device that is no longer connected over Classic Bluetooth.
void ReleaseSession(UINT64 btAddr);

// Reads the latest cached 1%-accurate AAP battery reading for btAddr.
bool GetReading(UINT64 btAddr, Snapshot& out);

// Cleanly disconnects and closes all open AAP driver handles and stops worker threads.
void Shutdown();

// Diagnostic text for `BatteryHub.exe --dump`.
std::wstring DumpDiagnostics();

}  // namespace airpods_aap
