#pragma once
// VGN peripherals: read-only battery queries.
//
// Every request below is a "Get…" command taken from VGN's own HUB client (VGN VHUB 4.0.11, the
// Electron renderer's plain-JS device classes; the same code ships in the web client at
// drive.vgnlab.com.cn). Nothing here sets a mode, writes flash or changes lighting.
//
// Kept free of Win32 so captured frames can be decoded in a unit test without hardware.
//
// Buffer conventions, matching HidDev in src/hid.h:
//   * Request bodies are built WITHOUT the report id; the caller prepends data[0] = report id.
//   * Replies are decoded from the buffer exactly as HidDev::Read/GetFeature returns it, so a
//     numbered reply still carries its report id at [0]. Reports whose id is 0 lose that byte.
#include <cstddef>
#include <cstdint>

namespace Vgn {

// One protocol family. Devices are mapped to a family by vendor/product id.
enum class Family {
    Da,          // vgn-y2 / vgn-f1-pm: F1 SE/MOBA, Y2 Ultra/Master/Pro Max, F2 Master
    King,        // vgn-king: Dragonfly King
    Kc2,         // vgn-kc2: Dragonfly 3 Turbo, F1 V5 Turbo, 3 MINI Turbo
    LdMs,        // vgn-ldMs: Falcon3 Extreme, F1 V2 Extreme
    Yongjiaxin,  // yongjiaxin: Dragonfly 3 SE/Pro, F1 V2 Pro/SE, Y2 V2 Pro
    Tjx,         // yjxkeyboard: N87
    Weisheng,    // weisheng: S99, V98Pro/V2, S87
    Beiying,     // beiying: V98Pro V3/V4, V87 V2, N75 V2, V108, VXE V87pro
    Rongyuan,    // rongyuan: Neon75 / Air / He / Turbo / Extreme, Neon68 Extreme
};

enum class Channel {
    Output,   // sendReport (HidDev::Write)
    Feature,  // sendFeatureReport + receiveFeatureReport (SetFeature / GetFeature)
};

struct DeviceEntry {
    uint16_t vid;
    uint16_t pid;
    Family family;
    Channel channel;
    uint16_t usagePage;
    int usage;
    uint8_t reportId;   // 0 when the device does not number its reports
    bool receiver;      // this id is the 2.4 GHz dongle rather than the device itself
};

const DeviceEntry* Find(uint16_t vid, uint16_t pid);
const wchar_t* FamilyKey(Family f);

// What a single reply told us.
struct Reading {
    bool answered = false;  // the device replied to the battery query
    int percent = -1;       // 0..100, -1 when the reply carries no usable level
    bool charging = false;
    int millivolts = 0;     // 0 when the family does not report voltage
    bool wired = false;     // the link is a cable, so the level is meaningless or the board charges
};

// ---- requests -------------------------------------------------------------------------------
// Each builder returns the frame length written to `out`; the caller prepends the report id and
// HidDev zero-pads to the collection's report length.
size_t BuildDaRequest(uint8_t* out, size_t cap, uint8_t cmd, uint8_t reportId);
size_t BuildKingRequest(uint8_t* out, size_t cap, uint8_t cmd);
size_t BuildKc2Request(uint8_t* out, size_t cap, uint8_t cmd);
size_t BuildLdMsRequest(uint8_t* out, size_t cap, uint8_t cmd, uint8_t sub);
size_t BuildYongjiaxinRequest(uint8_t* out, size_t cap, uint8_t cmd);
size_t BuildTjxRequest(uint8_t* out, size_t cap, uint8_t cmd);
size_t BuildWeishengRequest(uint8_t* out, size_t cap, uint8_t cmd);
size_t BuildBeiyingFeatureRequest(uint8_t* out, size_t cap, uint8_t cmd);
size_t BuildBeiyingOutputRequest(uint8_t* out, size_t cap, uint8_t cmd, uint8_t sub, uint8_t reportId);
size_t BuildRongyuanRequest(uint8_t* out, size_t cap, uint8_t cmd);

// ---- replies --------------------------------------------------------------------------------
// `reply`/`size` is what HidDev::Read or GetFeature returned.
Reading DecodeDa(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeKing(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeKc2(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeLdMs(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeYongjiaxin(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeTjx(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeWeisheng(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeBeiyingFeature(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeBeiyingOutput(const uint8_t* reply, size_t size, uint8_t reportId);
Reading DecodeRongyuan(const uint8_t* reply, size_t size, uint8_t reportId);

}  // namespace Vgn
