#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

// JBL Portable AA protocol, Flip 7 PID 2107. Reference:
// https://github.com/ffrafat/JBLControl/blob/main/docs/PROTOCOL-FLIP7.md
namespace jbl_speaker {
constexpr uint8_t request[] = {0xAA, 0x11, 0x00};
constexpr uint16_t product = 0x2107;
struct Info {
    int percent = -1;
    bool charging = false;
    uint16_t pid = 0;
    uint64_t address = 0;
    std::vector<uint8_t> pending;
    void Feed(const uint8_t* data, size_t size) {
        if (!size || size > 4096) return;
        if (pending.size() + size > 8192) pending.clear();
        pending.insert(pending.end(), data, data + size);
        for (;;) {
            auto start = std::find(pending.begin(), pending.end(), 0xAA);
            pending.erase(pending.begin(), start);
            if (pending.size() < 3) return;
            auto cmd = pending[1];
            bool longLen = cmd == 0x92 || (cmd >= 0x96 && cmd <= 0x99);
            size_t header = longLen ? 4 : 3;
            if (pending.size() < header) return;
            size_t len = longLen ? (pending[2] * 256u + pending[3]) : pending[2];
            if (len > 4096) { pending.erase(pending.begin()); continue; }
            if (pending.size() < header + len) return;
            const auto* p = pending.data() + header;
            if (cmd == 0x12 && len >= 3 && p[0] == 0) {
                if (p[1] == 0x44 && len == 3 && (p[2] & 0x7F) <= 100) {
                    percent = p[2] & 0x7F;
                    charging = (p[2] & 0x80) != 0;
                } else if (p[1] == 0x42 && len == 4) {
                    pid = static_cast<uint16_t>(p[2] * 256u + p[3]);
                } else if (p[1] == 0x48 && len == 8) {
                    address = 0;
                    for (int i = 2; i < 8; ++i) address = (address << 8) | p[i];
                }
            }
            pending.erase(pending.begin(), pending.begin() + header + len);
        }
    }
};
}
