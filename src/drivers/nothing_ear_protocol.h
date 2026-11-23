#pragma once
// Nothing / CMF Ear status frames over the Bluetooth RFCOMM (NT Link) channel.
// Kept independent of Win32 so captured frames can be unit-tested without the earbuds.
//
// Frame:  55 60 01 [cmd lo][cmd hi] [payload len] 00 [seq] [payload...] [crc lo][crc hi]
//   crc   = CRC-16/MODBUS (init 0xFFFF, reflected poly 0xA001) over bytes [0 .. 8+len-1], LE
//   query = cmd 0xC007, payload len 0 (read-only battery request)
//   reply = cmd 0xC007 (or 0xE001 push): payload[0] = device count N, followed by N pairs (id, status)
//           id: 0x02 left, 0x03 right, 0x04 case; status: bit 7 charging, bits 0..6 percent (0..100)
// Source: community reverse-engineered Nothing/CMF protocol (radiance-project/ear-web,
// Vadim-Karpenko/ear-tray, pedrordgs/nothing-ear).
#include <array>
#include <cstddef>
#include <cstdint>

namespace NothingEar {

constexpr std::uint16_t kCmdBattery = 0xC007;
constexpr std::uint16_t kCmdBatteryPush = 0xE001;

struct Status {
    int left = -1;       // -1 = not reported / not connected
    int right = -1;
    int caseLevel = -1;
    bool leftCharging = false;
    bool rightCharging = false;
    bool caseCharging = false;
};

inline std::uint16_t Crc16Modbus(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 1) ? (std::uint16_t)((crc >> 1) ^ 0xA001) : (std::uint16_t)(crc >> 1);
    }
    return crc;
}

inline std::array<std::uint8_t, 10> BuildBatteryQuery(std::uint8_t seq = 1) {
    std::array<std::uint8_t, 10> f = { 0x55, 0x60, 0x01, 0x07, 0xC0, 0x00, 0x00, seq, 0x00, 0x00 };
    std::uint16_t crc = Crc16Modbus(f.data(), 8);
    f[8] = (std::uint8_t)(crc & 0xFF);
    f[9] = (std::uint8_t)(crc >> 8);
    return f;
}

inline bool FindStatus(const std::uint8_t* data, std::size_t size, Status& out) {
    bool found = false;
    for (std::size_t i = 0; data && i + 10 <= size; ++i) {
        if (data[i] != 0x55 || data[i + 1] != 0x60 || data[i + 2] != 0x01) continue;
        std::size_t plen = data[i + 5];
        std::size_t total = 8 + plen + 2;
        if (i + total > size) continue;
        std::uint16_t crc = (std::uint16_t)(data[i + 8 + plen] | (data[i + 9 + plen] << 8));
        if (Crc16Modbus(data + i, 8 + plen) != crc) continue;
        std::uint16_t cmd = (std::uint16_t)(data[i + 3] | (data[i + 4] << 8));
        if (cmd != kCmdBattery && cmd != kCmdBatteryPush) {
            i += total - 1;
            continue;
        }
        const std::uint8_t* p = data + i + 8;
        if (plen < 1) continue;
        std::size_t count = p[0];
        if (1 + count * 2 > plen) continue;
        Status s;
        bool any = false;
        for (std::size_t k = 0; k < count; ++k) {
            std::uint8_t id = p[1 + k * 2];
            std::uint8_t st = p[2 + k * 2];
            int pct = st & 0x7F;
            bool chg = (st & 0x80) != 0;
            if (pct > 100) continue;
            if (id == 0x02) { s.left = pct; s.leftCharging = chg; any = true; }
            else if (id == 0x03) { s.right = pct; s.rightCharging = chg; any = true; }
            else if (id == 0x04) { s.caseLevel = pct; s.caseCharging = chg; any = true; }
        }
        if (any) {
            out = s;
            found = true;
        }
        i += total - 1;
    }
    return found;
}

}  // namespace NothingEar
