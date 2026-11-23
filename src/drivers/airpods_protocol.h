#pragma once
// Apple AirPods / Beats battery protocol decoders and BLE broadcast selection.
//
// Two transports are decoded here:
//   1. Apple Accessory Protocol (AAP) over Bluetooth Classic L2CAP PSM 0x1001, exposed to
//      user mode via Windows AAP kernel bridge drivers (NTPodsAAP / PodPilotAap / MagicAAP).
//      Reports true 1%-granular battery levels for single-battery headsets and dual earbuds + case.
//      Protocol details from librepods (docs/AAP Definitions.md), arctumn/ntpods, kylianxdev/Earlo,
//      and FIocker/aap-head-tracker.
//   2. Apple BLE Continuity "proximity pairing" advertisements (company 0x004C, type 0x07),
//      which broadcast 10%-step levels (nibbles 0..10) in cleartext. Layout from OpenPods /
//      AirPodsDesktop / librepods.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AirPodsAap {

inline constexpr uint16_t kPsm = 0x1001;
inline constexpr uint8_t kHeader[4] = { 0x04, 0x00, 0x04, 0x00 };
inline constexpr uint8_t kHandshake[16] = {
    0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
inline constexpr uint8_t kHostCapabilities[14] = {
    0x04, 0x00, 0x04, 0x00, 0x4D, 0x00, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
inline constexpr uint8_t kRequestNotifications[10] = {
    0x04, 0x00, 0x04, 0x00, 0x0F, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
};

struct BatteryReading {
    bool singleModel = false;
    int single = -1, left = -1, right = -1, box = -1;  // 0..100, -1 = unknown
    bool singleCharging = false, leftCharging = false, rightCharging = false, boxCharging = false;

    bool HasAny() const { return single >= 0 || left >= 0 || right >= 0 || box >= 0; }
};

// Parses an AAP battery notification frame:
//   [04 00 04 00] [04 00] [count] { [type] [01] [level 0..100] [status] [01] } * count
inline bool ParseBatteryPacket(const uint8_t* data, size_t len, BatteryReading& out) {
    if (!data || len < 7) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (data[i] != kHeader[i]) return false;
    }
    if (data[4] != 0x04 || data[5] != 0x00) return false;

    const size_t count = data[6];
    if (count < 1 || count > 4 || len < 7 + count * 5) return false;

    BatteryReading parsed;
    for (size_t i = 0; i < count; ++i) {
        const size_t off = 7 + i * 5;
        const uint8_t type = data[off];
        const uint8_t rawLevel = data[off + 2];
        const uint8_t rawStatus = data[off + 3];

        // 0x04 = disconnected, 0xFF = slot absent/unknown, (0x00, 0) = uninitialized sentinel.
        if (rawStatus == 0x04 || rawLevel == 0xFF || rawLevel > 100 || (rawStatus == 0x00 && rawLevel == 0))
            continue;

        // 0x01 = charging, 0x05 = charging in case.
        const bool charging = (rawStatus == 0x01 || rawStatus == 0x05);
        switch (type) {
            case 0x01:
                parsed.singleModel = true;
                parsed.single = rawLevel;
                parsed.singleCharging = charging;
                break;
            case 0x02:
                parsed.right = rawLevel;
                parsed.rightCharging = charging;
                break;
            case 0x04:
                parsed.left = rawLevel;
                parsed.leftCharging = charging;
                break;
            case 0x08:
                parsed.box = rawLevel;
                parsed.boxCharging = charging;
                break;
            default:
                break;
        }
    }

    if (!parsed.HasAny()) return false;
    out = parsed;
    return true;
}

// Merges newly reported components into `dst`, preserving previously known components
// that were omitted or disconnected in the latest partial packet (such as the closed case).
inline void MergeBattery(BatteryReading& dst, const BatteryReading& src) {
    if (src.singleModel) dst.singleModel = true;
    if (src.single >= 0) {
        dst.single = src.single;
        dst.singleCharging = src.singleCharging;
    }
    if (src.left >= 0) {
        dst.left = src.left;
        dst.leftCharging = src.leftCharging;
    }
    if (src.right >= 0) {
        dst.right = src.right;
        dst.rightCharging = src.rightCharging;
    }
    if (src.box >= 0) {
        dst.box = src.box;
        dst.boxCharging = src.boxCharging;
    }
}

// Extracts and merges all complete AAP battery packets from a byte stream buffer (used by
// stream-oriented bridges such as MagicAAP). Keeps incomplete trailing packets/prefixes in `buf`.
inline bool ExtractBatteryPackets(std::vector<uint8_t>& buf, BatteryReading& merged) {
    constexpr size_t kMaxBufferBytes = 4096;
    bool updated = false;
    size_t cursor = 0;
    bool waitMore = false;

    while (cursor + 4 <= buf.size()) {
        auto it = std::search(buf.begin() + static_cast<std::ptrdiff_t>(cursor), buf.end(),
                              std::begin(kHeader), std::end(kHeader));
        if (it == buf.end()) {
            cursor = buf.size();
            break;
        }

        const size_t pos = static_cast<size_t>(it - buf.begin());
        if (pos + 7 > buf.size()) {
            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(pos));
            waitMore = true;
            break;
        }

        if (buf[pos + 4] == 0x04 && buf[pos + 5] == 0x00) {
            const size_t count = buf[pos + 6];
            if (count >= 1 && count <= 4) {
                const size_t pktLen = 7 + count * 5;
                if (pos + pktLen > buf.size()) {
                    buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(pos));
                    waitMore = true;
                    break;
                }
                BatteryReading parsed;
                if (ParseBatteryPacket(buf.data() + pos, pktLen, parsed)) {
                    MergeBattery(merged, parsed);
                    updated = true;
                }
                cursor = pos + pktLen;
                continue;
            }
        }

        cursor = pos + 4;
    }

    if (!waitMore) {
        size_t keep = 0;
        const size_t avail = buf.size() - cursor;
        for (size_t k = std::min<size_t>(avail, 3); k > 0; --k) {
            if (std::equal(kHeader, kHeader + k, buf.end() - static_cast<std::ptrdiff_t>(k))) {
                keep = k;
                break;
            }
        }
        if (keep > 0) {
            buf.erase(buf.begin(), buf.end() - static_cast<std::ptrdiff_t>(keep));
        } else {
            buf.clear();
        }
    }

    if (buf.size() > kMaxBufferBytes) {
        buf.erase(buf.begin(), buf.end() - 3);
    }
    return updated;
}

}  // namespace AirPodsAap

namespace AirPodsBle {

struct Advert {
    uint16_t model = 0;
    bool single = false;
    bool leftBroadcasts = false;
    int left = -1, right = -1, box = -1;  // percent, -1 unknown (single-battery models use `left`)
    bool leftChg = false, rightChg = false, boxChg = false;
    int rssi = -127;
    uint64_t tick = 0;
};

inline int Level(int nibble) { return (nibble >= 0 && nibble <= 10) ? nibble * 10 : -1; }

// Single-battery headphones and neckbands (AirPods Max, Beats Solo/Studio/X/Flex/Powerbeats) put
// their battery in the low nibble of byte 6 and leave the upper nibble and case nibble as 0 (not
// 15), so decoding them as dual-bud would always report the unused side as 0%.
inline bool IsSingleBatteryModel(uint16_t model) {
    switch (model) {
        case 0x200A:  // AirPods Max (Lightning)
        case 0x201F:  // AirPods Max (USB-C)
        case 0x202D:  // AirPods Max 2
        case 0x2003:  // Powerbeats 3
        case 0x2005:  // BeatsX
        case 0x2006:  // Beats Solo 3
        case 0x2009:  // Beats Studio 3
        case 0x200C:  // Beats Solo Pro
        case 0x200D:  // Powerbeats 4
        case 0x2010:  // Beats Flex
        case 0x2017:  // Beats Studio Pro
        case 0x2025:  // Beats Solo 4
            return true;
        default:
            return false;
    }
}

inline bool ParseProximityTlv(const uint8_t* d, size_t n, Advert& a) {
    // d[1] == 0x19 (25 bytes following) and d[2] == 0x01 (paired status mode; 0x00 is pairing mode).
    if (!d || n < 27 || d[0] != 0x07 || d[1] != 0x19 || d[2] != 0x01) return false;
    a.model = static_cast<uint16_t>(d[3] | (d[4] << 8));
    if ((a.model & 0xFF00) != 0x2000) return false;
    a.single = IsSingleBatteryModel(a.model);
    a.leftBroadcasts = (d[5] & 0x20) != 0;
    const int cur = d[6] & 0x0F, other = d[6] >> 4;
    const int chg = d[7] >> 4;
    if (a.single) {
        int pct = Level(cur);
        bool charging = (chg & 0x01) != 0;
        if (cur == 0 && other >= 1 && other <= 10) {
            pct = Level(other);
            charging = (chg & 0x02) != 0;
        }
        if (pct < 0) return false;
        a.left = pct;
        a.leftChg = charging;
        a.right = -1;
        a.box = -1;
        a.boxChg = false;
        return true;
    }
    a.left = Level(a.leftBroadcasts ? cur : other);
    a.right = Level(a.leftBroadcasts ? other : cur);
    a.leftChg = a.left >= 0 && (chg & (a.leftBroadcasts ? 0x01 : 0x02)) != 0;
    a.rightChg = a.right >= 0 && (chg & (a.leftBroadcasts ? 0x02 : 0x01)) != 0;
    a.box = Level(d[7] & 0x0F);
    a.boxChg = a.box >= 0 && (chg & 0x04) != 0;
    return a.left >= 0 || a.right >= 0 || a.box >= 0;
}

inline bool Parse(const uint8_t* buf, size_t n, Advert& a) {
    // Walk the Apple Continuity TLV sequence in case another TLV precedes 0x07.
    if (!buf) return false;
    for (size_t i = 0; i + 1 < n;) {
        const uint8_t type = buf[i], len = buf[i + 1];
        if (i + 2 + static_cast<size_t>(len) > n) break;
        if (type == 0x07 && ParseProximityTlv(buf + i, 2 + static_cast<size_t>(len), a)) return true;
        i += 2 + static_cast<size_t>(len);
    }
    return false;
}

// Selects the best advertisement for a paired Apple audio device.
//
// AirPods broadcast every 1-2 s while rotating random BLE addresses. Using a 10 s active RSSI
// competition window ensures nearby simultaneous devices still arbitrate by RSSI, while stale
// high-RSSI addresses yield within 10 s instead of pinning an outdated reading for 90 s.
inline bool PickBestAdvert(const std::vector<Advert>& adverts, uint16_t pid, bool connected,
                           uint64_t nowTick, Advert& best) {
    constexpr uint64_t kActiveWindowMs = 10 * 1000;
    const uint64_t maxAge = connected ? 5ULL * 60 * 1000 : 90ULL * 1000;
    const int minRssi = connected ? -90 : -80;
    bool found = false;

    for (const auto& a : adverts) {
        if (pid && a.model != pid) continue;
        const uint64_t age = nowTick >= a.tick ? nowTick - a.tick : 0;
        if (age > maxAge || a.rssi < minRssi) continue;

        const bool active = age < kActiveWindowMs;
        const uint64_t bestAge = found && nowTick >= best.tick ? nowTick - best.tick : 0;
        const bool bestActive = found && bestAge < kActiveWindowMs;

        if (!found || (active && !bestActive) ||
            (active && bestActive && (a.rssi > best.rssi || (a.rssi == best.rssi && a.tick > best.tick))) ||
            (!active && !bestActive && a.tick > best.tick)) {
            best = a;
            found = true;
        }
    }

    if (found && !best.single) {
        const bool needLeft = best.left < 0;
        const bool needRight = best.right < 0;
        const bool needBox = best.box < 0;
        uint64_t fillLeftTick = 0, fillRightTick = 0, fillBoxTick = 0;

        for (const auto& a : adverts) {
            if (a.model != best.model || a.single) continue;
            if (a.rssi < minRssi) continue;
            const int rssiDiff = a.rssi >= best.rssi ? a.rssi - best.rssi : best.rssi - a.rssi;
            if (rssiDiff > 15) continue;
            const uint64_t dt = best.tick >= a.tick ? best.tick - a.tick : a.tick - best.tick;
            if (dt > 15 * 1000) continue;

            if (needLeft && a.left >= 0 && (best.left < 0 || a.tick >= fillLeftTick)) {
                best.left = a.left;
                best.leftChg = a.leftChg;
                fillLeftTick = a.tick;
            }
            if (needRight && a.right >= 0 && (best.right < 0 || a.tick >= fillRightTick)) {
                best.right = a.right;
                best.rightChg = a.rightChg;
                fillRightTick = a.tick;
            }
            if (needBox && a.box >= 0 && (best.box < 0 || a.tick >= fillBoxTick)) {
                best.box = a.box;
                best.boxChg = a.boxChg;
                fillBoxTick = a.tick;
            }
        }
    }

    return found;
}

}  // namespace AirPodsBle
