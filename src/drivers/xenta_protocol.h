#pragma once

#include <cstdint>
#include <vector>

namespace xenta_protocol {

using Bytes = std::vector<std::uint8_t>;

struct BatteryReport {
    std::uint8_t model = 0;
    std::uint8_t event = 0;
    std::uint8_t state = 0;
    std::uint8_t level = 0;
};

bool ParseBatteryReport(const Bytes& bytes, BatteryReport& out);

}  // namespace xenta_protocol