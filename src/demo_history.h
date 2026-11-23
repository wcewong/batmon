#pragma once
// Deterministic, synthetic battery histories for --demo only.
// Never used by hardware polling and never written to the history store.
#include "history_model.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace battery_history {

enum class DemoTrend { Discharging, Charging, Insufficient, Flat, Stale };

struct DemoScenario {
    const wchar_t* key;
    int currentPercent;
    int observationHours;
    int dischargePoints;
    int ageHours;
    DemoTrend trend;
    int phase;
};

inline std::vector<Sample> DemoSamples(const std::wstring& key, int64_t now) {
    // Keep these keys and percentages in sync with DemoDevices() in src/main.cpp.
    // Forecasts below are deliberately synthetic and are NOT device specifications.
    static constexpr DemoScenario scenarios[] = {
        {L"razer:demo",        78, 11,  6, 0, DemoTrend::Discharging,  0}, // ~6d
        {L"demo:logi",         64, 12,  4, 0, DemoTrend::Discharging, 13}, // ~8d
        {L"steelseries:demo",  55,  6, 18, 0, DemoTrend::Discharging, 27}, // ~18h
        {L"hsc:1b1c:0a6b",     81,  8,  8, 0, DemoTrend::Discharging, 39}, // ~3d
        {L"hyperx:demo",       47,  4,  4, 0, DemoTrend::Discharging, 51}, // ~47h
        {L"rog:demo",          90, 12,  3, 0, DemoTrend::Discharging, 63}, // ~15d
        {L"zowie:demo",        66, 10,  9, 0, DemoTrend::Discharging, 75}, // ~3d
        {L"bt:demo-ps",        38,  5, 19, 0, DemoTrend::Discharging, 87}, // ~10h
        {L"xbox:demo",         72,  3, 24, 0, DemoTrend::Discharging, 99}, // ~9h
        {L"bt:demo-buds",      59,  2,  1, 0, DemoTrend::Insufficient, 11},
        {L"airpods:demo",      85,  8,  0, 0, DemoTrend::Flat,         23},
        {L"jabra:demo",        23,  8, 16, 5, DemoTrend::Stale,        35},
        {L"jbl:demo",          70,  8,  5, 0, DemoTrend::Discharging, 47}, // ~5d
        {L"coolermaster:demo", 12,  6,  9, 0, DemoTrend::Discharging, 59}, // ~8h
        {L"steam:demo",        50,  8,  8, 0, DemoTrend::Charging,     71},
        {L"bt:demo-plain",    100,  8,  0, 0, DemoTrend::Charging,     83},
    };

    const auto found = std::find_if(std::begin(scenarios), std::end(scenarios),
        [&key](const DemoScenario& s) { return key == s.key; });
    if (found == std::end(scenarios)) return {};

    const DemoScenario& cfg = *found;
    std::vector<Sample> samples;

    // Historical charge cycles keep the 7/30-day viewer useful. A deliberate
    // >4-hour gap separates them from the current discharge session.
    const int64_t oldStart = now - 30LL * 24 * 3600;
    const int64_t oldEnd = now -
        static_cast<int64_t>(cfg.ageHours + cfg.observationHours + 8) * 3600;
    for (int64_t time = oldStart; time <= oldEnd; time += 3 * 3600) {
        const int64_t hours = (time - oldStart) / 3600;
        const int cycle = static_cast<int>((hours + cfg.phase) % 120);
        const bool charging = cycle >= 108 && cycle <= 114;
        const int percent = cycle < 108 ? 96 - cycle / 2 :
                            cycle == 108 ? 60 : cycle == 111 ? 78 : 96;
        samples.push_back({key, time, percent, charging});
    }

    const int count = cfg.trend == DemoTrend::Insufficient ? 2 : 5;
    const int steps = count - 1;
    for (int i = 0; i < count; ++i) {
        const int remainingSteps = steps - i;
        const int64_t secondsAgo = static_cast<int64_t>(cfg.ageHours) * 3600 +
            static_cast<int64_t>(cfg.observationHours) * 3600 * remainingSteps / steps;
        const int percent = cfg.currentPercent +
            (cfg.dischargePoints * remainingSteps + steps / 2) / steps;
        const bool charging = cfg.trend == DemoTrend::Charging && i == steps;
        samples.push_back({key, now - secondsAgo, percent, charging});
    }
    return samples;
}

} // namespace battery_history
