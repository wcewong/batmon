#pragma once
// ARDOR GAMING Fury V2 (receiver VID:PID 1D57:FA60; usage 000A:0000).
// Observed 5-byte input reports:
//   03 00 40 33 07 -> 50%
//   03 00 40 73 08 -> 75%
// The fifth byte changes between verified charge levels and MUST NOT be
// treated as a fixed signature. The device identification is enforced by
// the caller (FA60 + usage 000A), and this decoder checks the remaining
// report shape. 25% and 100% mappings are inferred from vendor logic.
#include <cstddef>
#include <cstdint>

inline int ArdorFuryV2Percent(const uint8_t* r, std::size_t n) {
    if (!r || n < 5 || r[0] != 0x03 || r[1] != 0x00 || r[2] != 0x40 ||
        (r[3] & 0x0F) != 0x03)
        return -1;
    switch (r[3] >> 4) {
        case 0x1: return 25;  // inferred; not yet observed on this mouse
        case 0x3: return 50;  // observed: 03 00 40 33 07
        case 0x7: return 75;  // observed: 03 00 40 73 08
        case 0xF: return 100; // inferred; not yet observed on this mouse
        default: return -1;
    }
}
