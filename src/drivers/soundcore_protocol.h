#pragma once
// Anker Soundcore Bluetooth RFCOMM packet framing and battery decoding.
// Kept independent of Win32 so captured packets can be unit-tested without hardware.
//
// Packet layout (little-endian length, 1-byte additive checksum over all preceding bytes):
//   Outbound (host -> device): 08 EE 00 00 00 [cmd0] [cmd1] [lenLo] [lenHi] [body...] [checksum]
//   Inbound  (device -> host): 09 FF 00 00 01 [cmd0] [cmd1] [lenLo] [lenHi] [body...] [checksum]
//   len = 5 (header) + 2 (cmd) + 2 (len) + body.size() + 1 (checksum) = 10 + body.size().
//
// Commands used (read-only):
//   01 03  RequestBatteryLevel     -> [level] (headphones) or [left, right, ...] (TWS earbuds, 0xFF = disconnected)
//   01 04  RequestBatteryCharging  -> [charging] or [leftCharging, rightCharging] (1 = charging)
//   01 05  RequestSerialAndFw      -> firmware + serial string containing the 4-digit model code (e.g. "3028")
//   01 01  RequestState            -> fallback when 01 03 is unsupported; starts with SingleBattery or TwsStatus+DualBattery
//
// Protocol & per-model battery scales verified against OpenSCQ30 (Oppzippy/OpenSCQ30)
// and live captures from soundcore Life Q30 (A3028, firmware 04.33).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Soundcore {

struct Command {
    std::uint8_t group;
    std::uint8_t id;
    bool operator==(const Command& o) const { return group == o.group && id == o.id; }
};

constexpr Command kCmdState = { 0x01, 0x01 };
constexpr Command kCmdBatteryLevel = { 0x01, 0x03 };
constexpr Command kCmdBatteryCharging = { 0x01, 0x04 };
constexpr Command kCmdSerialAndFw = { 0x01, 0x05 };

struct Scale {
    int maxLevel = 5;     // divisor after adding offset
    int levelOffset = 0;  // added to raw value before dividing (e.g. 0..9 -> 10%..100%)
    bool known = false;   // true when derived from an identified model code
};

struct Packet {
    Command cmd{};
    std::vector<std::uint8_t> body;
};

struct BatteryStatus {
    bool dual = false;
    int percent = -1;     // single-battery percentage (0..100), or -1 if unknown
    int left = -1;        // TWS left percentage (0..100), or -1 if disconnected/unknown
    int right = -1;       // TWS right percentage (0..100), or -1 if disconnected/unknown
    bool charging = false;
    bool leftCharging = false;
    bool rightCharging = false;
};

inline std::uint8_t Checksum(const std::uint8_t* data, std::size_t size) {
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < size; i++) sum = (std::uint8_t)(sum + data[i]);
    return sum;
}

inline std::array<std::uint8_t, 10> BuildEmptyRequest(Command cmd) {
    std::array<std::uint8_t, 10> pkt = {
        0x08, 0xEE, 0x00, 0x00, 0x00, cmd.group, cmd.id, 0x0A, 0x00, 0x00
    };
    pkt[9] = Checksum(pkt.data(), 9);
    return pkt;
}

// Finds the last valid inbound packet matching `wanted` in `data[0..size)`.
// Validates header (09 FF 00 00 01), total length, and trailing additive checksum.
inline bool FindInboundPacket(const std::uint8_t* data, std::size_t size, Command wanted, Packet& out) {
    bool found = false;
    if (!data || size < 10) return false;
    for (std::size_t i = 0; i + 10 <= size; i++) {
        if (data[i] != 0x09 || data[i + 1] != 0xFF || data[i + 2] != 0x00 || data[i + 3] != 0x00 || data[i + 4] != 0x01)
            continue;
        std::size_t totalLen = (std::size_t)data[i + 7] | ((std::size_t)data[i + 8] << 8);
        if (totalLen < 10 || totalLen > 1024 || i + totalLen > size) continue;
        if (Checksum(data + i, totalLen - 1) != data[i + totalLen - 1]) continue;
        Command cmd{ data[i + 5], data[i + 6] };
        if (cmd == wanted) {
            out.cmd = cmd;
            out.body.assign(data + i + 9, data + i + totalLen - 1);
            found = true;
        }
        i += totalLen - 1;  // loop increment advances past this packet
    }
    return found;
}

// Maps an Anker Soundcore 4-digit model code (e.g. "3028" for A3028 Life Q30) to its battery scale.
// Models not using the common 0..5 scale (verified from OpenSCQ30):
//   3005 (Q20i variant), 3062 (Space One Pro), 3388, 3876, 3957 (Liberty 5): 0..9 -> 10%..100%
//   3959 (P30i / R50i NC): 0..10 -> 0%..100%
//   3954 (Liberty 4 Pro):  0..100 -> 0%..100%
inline Scale ScaleForModelCode(const std::string& code) {
    if (code == "3005" || code == "3062" || code == "3388" || code == "3876" || code == "3957")
        return { 10, 1, true };
    if (code == "3959")
        return { 10, 0, true };
    if (code == "3954")
        return { 100, 0, true };
    if (code.size() == 4 && code[0] == '3' &&
        code[1] >= '0' && code[1] <= '9' && code[2] >= '0' && code[2] <= '9' && code[3] >= '0' && code[3] <= '9')
        return { 5, 0, true };
    return {};
}

// Infers a known non-default scale from the Bluetooth friendly name (lowercase ASCII) when no
// serial/model code is available.
inline Scale ScaleFromNameLower(const std::string& lowerName) {
    if (lowerName.find("space one pro") != std::string::npos ||
        lowerName.find("liberty 5") != std::string::npos ||
        lowerName.find("aeroclip") != std::string::npos)
        return { 10, 1, true };
    if (lowerName.find("liberty 4 pro") != std::string::npos)
        return { 100, 0, true };
    if (lowerName.find("p30i") != std::string::npos ||
        lowerName.find("r50i nc") != std::string::npos)
        return { 10, 0, true };
    return {};
}

// Returns true for known TWS dual-battery model codes (39xx, 3031, 3330, 3388, 3876).
inline bool IsDualModelCode(const std::string& code) {
    if (code.size() != 4 || code[0] != '3') return false;
    return code[1] == '9' || code == "3031" || code == "3330" || code == "3388" || code == "3876";
}

// Scans a binary payload (01 05 serial response or 01 01 state update) for a 16-char ASCII Soundcore
// serial number beginning with a 4-digit model code ("3xxx" followed by 12 uppercase hex/alnum chars).
// Firmware versions ("XX.YY", or "XX.YYXX.YY" on dual-firmware earbuds) sit right before the serial
// number, so check right after a firmware string first to avoid matching trailing firmware digits.
inline std::string ExtractModelCode(const std::vector<std::uint8_t>& body) {
    auto isDigit = [](std::uint8_t c) { return c >= '0' && c <= '9'; };
    auto isAlnumUpper = [](std::uint8_t c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'); };
    auto isSerialAt = [&](std::size_t pos) {
        if (pos + 16 > body.size()) return false;
        if (body[pos] != '3' || !isDigit(body[pos + 1]) || !isDigit(body[pos + 2]) || !isDigit(body[pos + 3])) return false;
        for (std::size_t j = 4; j < 16; j++)
            if (!isAlnumUpper(body[pos + j])) return false;
        return true;
    };
    auto isFwAt = [&](std::size_t pos) {
        return pos + 5 <= body.size() && isDigit(body[pos]) && isDigit(body[pos + 1]) && body[pos + 2] == '.' &&
               isDigit(body[pos + 3]) && isDigit(body[pos + 4]);
    };
    for (std::size_t i = 0; i + 21 <= body.size(); i++) {
        if (!isFwAt(i)) continue;
        std::size_t sn = i + 5;
        if (isFwAt(sn)) sn += 5;  // dual firmware: "01.2301.23" + 16-char serial
        if (isSerialAt(sn)) return std::string((const char*)(body.data() + sn), 4);
    }
    for (std::size_t i = 0; i + 16 <= body.size(); i++) {
        if (i > 0 && (isDigit(body[i - 1]) || body[i - 1] == '.')) continue;
        if (isSerialAt(i)) return std::string((const char*)(body.data() + i), 4);
    }
    return "";
}

// Converts a raw Soundcore battery byte into 0..100%, or -1 for 0xFF (disconnected earbud) / invalid.
inline int RawToPercent(std::uint8_t raw, Scale scale) {
    if (raw == 0xFF) return -1;
    if (scale.known) {
        int val = (int)raw + scale.levelOffset;
        if (val < 0 || scale.maxLevel <= 0) return -1;
        return std::clamp(val * 100 / scale.maxLevel, 0, 100);
    }
    // Unknown model fallback: auto-adapt if raw exceeds the common 0..5 range.
    if (raw <= 5) return (int)raw * 20;
    if (raw <= 10) return (int)raw * 10;
    if (raw <= 100) return (int)raw;
    return -1;
}

// Parses a 01 03 (RequestBatteryLevel) response body into `out`.
inline bool ParseBatteryLevelBody(const std::vector<std::uint8_t>& body, Scale scale, BatteryStatus& out) {
    if (body.empty()) return false;
    if (body.size() == 1) {
        int pct = RawToPercent(body[0], scale);
        if (pct < 0) return false;
        out.dual = false;
        out.percent = pct;
        return true;
    }
    // 2 bytes: [left, right]; 4 bytes: [left, right, newLeft, newRight]
    int left = RawToPercent(body[0], scale);
    int right = RawToPercent(body[1], scale);
    if (left < 0 && right < 0) return false;
    out.dual = true;
    out.left = left;
    out.right = right;
    if (left >= 0 && right >= 0) out.percent = std::min(left, right);
    else out.percent = left >= 0 ? left : right;
    return true;
}

// Applies a 01 04 (RequestBatteryCharging) response body to `out`.
inline bool ApplyBatteryChargingBody(const std::vector<std::uint8_t>& body, BatteryStatus& out) {
    if (body.empty()) return false;
    if (body.size() == 1) {
        out.charging = body[0] == 1;
        return true;
    }
    out.leftCharging = body[0] == 1;
    out.rightCharging = body[1] == 1;
    out.charging = (out.left >= 0 && out.leftCharging) || (out.right >= 0 && out.rightCharging);
    return true;
}

// Fallback parser for a 01 01 (RequestState) response body when 01 03 is not answered.
// Single-battery headphones start with [level, isCharging]; TWS earbuds start with
// [twsConnected, hostDevice, leftLevel, rightLevel, leftCharging, rightCharging].
inline bool ParseStateUpdateBody(const std::vector<std::uint8_t>& body, Scale scale, bool isDual, BatteryStatus& out) {
    if (isDual) {
        if (body.size() < 6) return false;
        int left = RawToPercent(body[2], scale);
        int right = RawToPercent(body[3], scale);
        if (left < 0 && right < 0) return false;
        out.dual = true;
        out.left = left;
        out.right = right;
        out.percent = (left >= 0 && right >= 0) ? std::min(left, right) : (left >= 0 ? left : right);
        out.leftCharging = body[4] == 1;
        out.rightCharging = body[5] == 1;
        out.charging = (left >= 0 && out.leftCharging) || (right >= 0 && out.rightCharging);
        return true;
    }
    if (body.size() < 2) return false;
    int pct = RawToPercent(body[0], scale);
    if (pct < 0) return false;
    out.dual = false;
    out.percent = pct;
    out.charging = body[1] == 1;
    return true;
}

// Parses Google FastPair Message Stream packets (Group 0x03, Code 0x03: Battery Updated).
// Payload can be 1 byte (single headphone) or >=2 bytes (TWS left, right, case).
// Bit 7 indicates charging, bits 0-6 indicate percentage (0..100, 0x7F = disconnected).
inline bool ParseFastPairStream(const std::uint8_t* data, std::size_t size, BatteryStatus& out) {
    std::size_t i = 0;
    bool found = false;
    while (i + 4 <= size) {
        std::uint8_t grp = data[i];
        std::uint8_t code = data[i + 1];
        std::size_t len = ((std::size_t)data[i + 2] << 8) | data[i + 3];
        if (i + 4 + len > size) break;
        const std::uint8_t* payload = data + i + 4;
        if (grp == 0x03 && code == 0x03) {
            if (len == 1) {
                std::uint8_t b = payload[0];
                out.charging = (b & 0x80) != 0;
                int val = b & 0x7F;
                if (val <= 100) { out.percent = val; out.dual = false; found = true; }
            } else if (len >= 2) {
                out.dual = true;
                std::uint8_t bL = payload[0];
                std::uint8_t bR = payload[1];
                int pL = (bL & 0x7F) <= 100 ? (int)(bL & 0x7F) : -1;
                int pR = (bR & 0x7F) <= 100 ? (int)(bR & 0x7F) : -1;
                out.left = pL;
                out.leftCharging = (bL & 0x80) != 0;
                out.right = pR;
                out.rightCharging = (bR & 0x80) != 0;
                if (pL >= 0 && pR >= 0) out.percent = std::min(pL, pR);
                else if (pL >= 0) out.percent = pL;
                else if (pR >= 0) out.percent = pR;
                out.charging = (pL >= 0 && out.leftCharging) || (pR >= 0 && out.rightCharging);
                found = out.percent >= 0;
            }
        }
        i += 4 + len;
    }
    return found;
}

}  // namespace Soundcore
