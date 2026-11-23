#pragma once
#include <vector>

namespace AulaMouseProtocol {
inline std::vector<unsigned char> Request(unsigned char cmd) {
    std::vector<unsigned char> r(64, 0);
    r[0] = 4;
    r[3] = cmd;
    return r;
}
inline bool IsHeartbeat(const std::vector<unsigned char>& r) {
    return r.size() >= 9 && r[0] == 4 && r[1] == 0xAA && r[2] == 0 && r[3] == 0xAA &&
           (r[8] == 0 || r[8] == 0xFF);
}
inline bool IsReply(const std::vector<unsigned char>& r, unsigned char cmd) {
    return r.size() >= 8 && r[0] == 4 && r[1] == 0 && r[2] == 0 && r[3] == cmd &&
           r[4] == 0 && r[5] == 0 && r[6] == 0 && r[7] == 0;
}
inline bool ParseBattery(const std::vector<unsigned char>& r, int& percent, bool& charging) {
    if (r.size() < 10 || !IsReply(r, 0x1A) || r[8] == 0 || r[8] > 100 || r[9] > 2) return false;
    percent = r[8];
    charging = r[9] != 0;
    return true;
}
}  // namespace AulaMouseProtocol
