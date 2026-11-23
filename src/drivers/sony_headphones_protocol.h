#pragma once
// Sony Bluetooth headphones / earbuds ("MDR" protocol used by the Sony Headphones Connect app).
// Kept independent of Win32 so captured frames can be checked without the headphones.
//
// Frame:   3E [type][seq][len: 4 bytes big-endian][payload...][checksum] 3C
//   checksum = sum of type, seq, len and payload bytes, low 8 bits
//   inside the frame 3C / 3D / 3E are escaped as 3D followed by (byte & 0xEF)
// Types:   0x01 ACK (no payload, seq = 1 - acknowledged seq), 0x0C command / reply
// Read-only requests sent by the driver:
//   00 00         protocol info (the headphones answer 01 ...; some ignore requests until asked)
//   10 <type>     battery level, type 00 = headphones, 01 = left + right earbuds, 0A = case
// Replies:
//   11 00 <level> <charging>                                   single battery
//   11 01 <left> <left charging> <right> <right charging>      earbuds (0 = earbud not connected)
//   11 0A <level> <charging>                                   case
// Source: Gadgetbridge (devices/sony/headphones: Message.java, MessageType.java,
// protocol/impl/v1/SonyProtocolImplV1.java, PayloadTypeV1.java, BatteryType.java).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SonyMdr {

constexpr std::uint8_t kStart = 0x3E, kEnd = 0x3C, kEscape = 0x3D;
constexpr std::uint8_t kTypeAck = 0x01, kTypeCommand = 0x0C;
constexpr std::uint8_t kBatterySingle = 0x00, kBatteryDual = 0x01, kBatteryCase = 0x0A;

struct Message {
    std::uint8_t type = 0;
    std::uint8_t seq = 0;
    std::vector<std::uint8_t> payload;
};

inline std::vector<std::uint8_t> Encode(std::uint8_t type, std::uint8_t seq, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> raw = { type, seq };
    std::uint32_t n = (std::uint32_t)payload.size();
    raw.push_back((std::uint8_t)(n >> 24));
    raw.push_back((std::uint8_t)(n >> 16));
    raw.push_back((std::uint8_t)(n >> 8));
    raw.push_back((std::uint8_t)n);
    raw.insert(raw.end(), payload.begin(), payload.end());
    std::uint8_t sum = 0;
    for (std::uint8_t b : raw) sum = (std::uint8_t)(sum + b);
    raw.push_back(sum);
    std::vector<std::uint8_t> out = { kStart };
    for (std::uint8_t b : raw) {
        if (b == kStart || b == kEnd || b == kEscape) {
            out.push_back(kEscape);
            out.push_back((std::uint8_t)(b & 0xEF));
        } else {
            out.push_back(b);
        }
    }
    out.push_back(kEnd);
    return out;
}

// Takes the first complete, valid frame out of `buf` (dropping anything before it).
// Returns false when no complete frame is buffered yet.
inline bool TakeMessage(std::vector<std::uint8_t>& buf, Message& msg) {
    for (;;) {
        std::size_t start = 0;
        while (start < buf.size() && buf[start] != kStart) start++;
        if (start) buf.erase(buf.begin(), buf.begin() + start);
        if (buf.empty()) return false;
        std::size_t end = 1;
        while (end < buf.size() && buf[end] != kEnd && buf[end] != kStart) end++;
        if (end >= buf.size()) return false;           // frame not complete yet
        if (buf[end] == kStart) {                      // truncated frame: resync on the next start byte
            buf.erase(buf.begin(), buf.begin() + end);
            continue;
        }
        std::vector<std::uint8_t> raw;
        bool bad = false;
        for (std::size_t i = 1; i < end; i++) {
            if (buf[i] == kEscape) {
                if (++i >= end) { bad = true; break; }
                raw.push_back((std::uint8_t)(buf[i] | 0x10));
            } else {
                raw.push_back(buf[i]);
            }
        }
        buf.erase(buf.begin(), buf.begin() + end + 1);
        if (bad || raw.size() < 7) continue;
        std::uint32_t n = ((std::uint32_t)raw[2] << 24) | ((std::uint32_t)raw[3] << 16) | ((std::uint32_t)raw[4] << 8) | raw[5];
        if (n != raw.size() - 7) continue;
        std::uint8_t sum = 0;
        for (std::size_t i = 0; i + 1 < raw.size(); i++) sum = (std::uint8_t)(sum + raw[i]);
        if (sum != raw.back()) continue;
        msg.type = raw[0];
        msg.seq = raw[1];
        msg.payload.assign(raw.begin() + 6, raw.end() - 1);
        return true;
    }
}

struct Battery {
    int level = -1;      // headphones, or the lower earbud
    int left = -1, right = -1, caseLevel = -1;
    bool charging = false;
    bool dual = false;
};

inline int Level(std::uint8_t v) { return v <= 100 ? v : -1; }

// Applies a battery reply (payload starting with 0x11) to `b`. Returns false for other payloads.
inline bool ParseBattery(const std::vector<std::uint8_t>& p, Battery& b) {
    if (p.size() < 4 || p[0] != 0x11) return false;
    if (p[1] == kBatterySingle) {
        b.level = Level(p[2]);
        b.charging = p[3] == 1;
        return b.level >= 0;
    }
    if (p[1] == kBatteryDual && p.size() >= 6) {
        b.dual = true;
        b.left = p[2] >= 1 ? Level(p[2]) : -1;
        b.right = p[4] >= 1 ? Level(p[4]) : -1;
        b.charging = p[3] == 1 || p[5] == 1;
        if (b.left >= 0 && b.right >= 0) b.level = b.left < b.right ? b.left : b.right;
        else b.level = b.left >= 0 ? b.left : b.right;
        return b.level >= 0;
    }
    if (p[1] == kBatteryCase) {
        b.caseLevel = p[2] >= 1 ? Level(p[2]) : -1;
        return true;
    }
    return false;
}

}  // namespace SonyMdr
