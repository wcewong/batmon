#pragma once
#include <string>
#include <vector>

#include "devices.h"

// One device as exposed to external tools (YASB, Rainmeter, scripts, ...). Kept separate from
// DeviceStatus so the JSON/snapshot code does not need the tray state in main.cpp.
struct StatusDevice {
    std::wstring key;
    std::wstring name;
    std::wstring via;
    std::wstring detail;
    std::wstring levelText;
    DeviceKind kind = DeviceKind::Other;
    int  percent    = -1;   // live percentage from this poll, -1 when unknown right now
    int  lastPct    = -1;   // last known percentage, kept while the device sleeps
    int  millivolts = 0;
    bool charging   = false;
    bool asleep     = false;  // percent is the last known value, the device is sleeping
    bool connected  = true;
};

// Stable machine-readable device kind: "headset", "mouse", "keyboard", "gamepad", "phone", "other".
const char* KindId(DeviceKind kind);

// A UTF-8 JSON string literal, including the surrounding quotes.
std::string JsonQuote(const std::wstring& text);

// The document shared by `--json` and status.json. `source` is "cli" or "snapshot"; `updated` is
// passed in so tests can be deterministic (see IsoTimestampNow).
std::string BuildStatusJson(const std::vector<StatusDevice>& devices, const std::string& source,
                            int lowThreshold, const std::string& updated);

// Current UTC time as "2026-11-17T12:34:56Z".
std::string IsoTimestampNow();

// %LOCALAPPDATA%\WirelessBattery\status.json (falls back to %TEMP%).
std::wstring StatusSnapshotPath();

// Writes the snapshot atomically (temp file, then rename). Failures are silent: the app must keep
// working when the folder is read-only.
bool WriteStatusSnapshot(const std::vector<StatusDevice>& devices, const std::string& source, int lowThreshold);
