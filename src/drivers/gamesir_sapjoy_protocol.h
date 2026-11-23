#pragma once
// GameSir SAPJoy "device info" frame (usage page 0xFFA0) — read-only battery query.
//
// Wire format taken from the captured traffic of GameSir's own client on a NovaPro / Super Nova
// (T4NPro) and cross-checked with its main.js decoder. Nothing here writes flash, a profile or
// lighting.
//
// Frame (no checksum):
//   host -> pad : [0x07][len][cmd][sub][payload...]  zero padded to 32 bytes (OutputReportByteLength)
//   pad  -> host: [0x06][len][cmd][sub][payload...]  zero padded to 64 bytes (InputReportByteLength)
//   len counts itself and excludes the report id.
//
// GetDeviceInfo: cmd 0x0A, sub 0x01, empty payload.
//   reply [4] id, [5..6] firmware, [7..12] MAC, [13] battery, [14] type
//   battery 0..100 = percent, 255 = charging, >100 (and not 255) is clamped to 100 by the vendor app.
//
// Kept free of Win32 so a captured reply can be decoded in a unit test without hardware.
#include <array>
#include <cstddef>
#include <cstdint>

namespace GameSirSapjoy {

constexpr std::uint8_t kOutputReportId = 0x07;
constexpr std::uint8_t kInputReportId = 0x06;
constexpr std::uint8_t kCmdDeviceInfo = 0x0A;
constexpr std::uint8_t kSubDeviceInfo = 0x01;
constexpr std::size_t kOutputReportLen = 32;
constexpr std::size_t kInputReportLen = 64;

struct Reading {
    std::array<std::uint8_t, 6> address{};
    bool answered = false;
    int percent = -1;       // -1 when the reply carries no usable level (charging or out of range)
    bool charging = false;
    std::uint8_t deviceId = 0;
    std::uint8_t deviceType = 0;
};

// Fills `out` with the complete frame HidDev::Write expects: data[0] = report id, then the frame,
// zero padded to kOutputReportLen. Returns false when `cap` is smaller than kOutputReportLen.
bool BuildDeviceInfoRequest(std::uint8_t* out, std::size_t cap);

// Decodes one input report. `size` is what HidDev::Read returned, `reply[0]` is the report id.
// Returns a Reading with answered=false for anything that is not a GetDeviceInfo reply.
Reading DecodeDeviceInfo(const std::uint8_t* reply, std::size_t size);

}  // namespace GameSirSapjoy
