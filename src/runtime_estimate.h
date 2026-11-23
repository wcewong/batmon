#pragma once
// Conservative, local-only projection based on real observed discharge samples.
// This is a rough trend, not a device-reported runtime or a battery-health measurement.
#include "history_model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace battery_history {

enum class RuntimeState { Ready, Charging, Stale, Insufficient, Flat };

struct RuntimeEstimate {
    RuntimeState state = RuntimeState::Insufficient;
    double hoursRemaining = 0;
    double percentPerHour = 0;
    size_t sampleCount = 0;
    double observationHours = 0;
};

// The UI must never show a forecast as an actual measurement.
inline RuntimeEstimate EstimateRuntime(const std::vector<Sample>& input, int64_t now) {
    RuntimeEstimate out;
    if (input.empty()) return out;
    std::vector<Sample> points = input;
    std::stable_sort(points.begin(), points.end(), [](const Sample& a, const Sample& b) {
        return a.timestamp < b.timestamp;
    });
    const Sample& latest = points.back();
    if (latest.timestamp > now + 300 || now - latest.timestamp > 3 * 3600) {
        out.state = RuntimeState::Stale;
        return out;
    }
    if (latest.charging) {
        out.state = RuntimeState::Charging;
        return out;
    }
    if (latest.percent == 0) {
        out.state = RuntimeState::Ready;
        out.sampleCount = 1;
        return out;
    }
    size_t start = points.size() - 1;
    while (start > 0) {
        const Sample& previous = points[start - 1];
        const Sample& current = points[start];
        const int64_t gap = current.timestamp - previous.timestamp;
        const int difference = current.percent - previous.percent;
        if (previous.charging || current.charging || gap <= 0 || gap > 4 * 3600 ||
            difference > 1 || difference < -25) break;
        --start;
    }
    out.sampleCount = points.size() - start;
    if (out.sampleCount < 4) return out;
    out.observationHours = static_cast<double>(latest.timestamp - points[start].timestamp) / 3600.0;
    if (out.observationHours < 2.0) return out;
    const int drop = points[start].percent - latest.percent;
    if (drop < 3) { out.state = RuntimeState::Flat; return out; }
    out.percentPerHour = static_cast<double>(drop) / out.observationHours;
    // Wildly fast drops are normally corrupted telemetry, while extremely slow trends
    // are too sensitive to integer percentage quantization for a useful prediction.
    if (out.percentPerHour < 0.02 || out.percentPerHour > 12.0) {
        out.state = RuntimeState::Flat;
        return out;
    }
    out.hoursRemaining = static_cast<double>(latest.percent) / out.percentPerHour;
    if (!std::isfinite(out.hoursRemaining) || out.hoursRemaining > 90.0 * 24.0) {
        out.state = RuntimeState::Flat;
        return out;
    }
    out.state = RuntimeState::Ready;
    return out;
}

// Compact badge for the taskbar; the ~ prefix differentiates a prediction from a measured percentage.
// Return an empty string for any non-ready forecast, allowing a safe % fallback.
inline std::wstring CompactRuntimeText(const RuntimeEstimate& forecast) {
    if (forecast.state != RuntimeState::Ready ||
        !std::isfinite(forecast.hoursRemaining) ||
        forecast.hoursRemaining < 0 ||
        forecast.hoursRemaining > 90.0 * 24.0)
        return {};

    const double hours = forecast.hoursRemaining;

    if (hours > 0 && hours < 1)
        return L"<1h";

    if (hours >= 48)
        return L"~" +
            std::to_wstring(static_cast<long long>(std::llround(hours / 24.0))) +
            L"d";

    return L"~" +
        std::to_wstring(static_cast<long long>(std::llround(hours))) +
        L"h";
}

} // namespace battery_history
