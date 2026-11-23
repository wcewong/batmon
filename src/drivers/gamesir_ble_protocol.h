#pragma once
// GameSir Cyclone Pro "base info" packet, read over Bluetooth LE (see gamesir_ble.cpp).
//   [0..5] MAC  [6..7] hw ver  [8..9] BT ver  [10..11] fw ver  [12..17] build date  [18] battery
// Battery is 0..100 percent, or 255 while charging. Some firmwares prefix the packet with C9.
#include <cstddef>
#include <cstdint>

namespace GameSirBle {

struct Reading {
    int percent = -1;      // 0..100, -1 while charging (the pad reports no level then)
    bool charging = false;
};

// Returns false for anything that is not a base-info packet.
inline bool Parse(const std::uint8_t* p, std::size_t n, Reading& out) {
    if (n == 20 && p[0] == 0xC9) { p++; n--; }
    if (n != 19) return false;
    std::uint8_t v = p[18];
    if (v == 0xFF) { out.percent = -1; out.charging = true; return true; }
    if (v > 100) return false;
    out.percent = v;
    out.charging = false;
    return true;
}

}  // namespace GameSirBle
