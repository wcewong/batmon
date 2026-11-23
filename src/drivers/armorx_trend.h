#pragma once
// The Armor X Pro sends no charging flag over Bluetooth (only the 0x2A19 level), so charging is
// guessed from the trend. Readings are median-smoothed to ride out the jitter of a tired battery,
// and charging needs a sustained rise, not one bounce. The controller's LED stays the ground truth
// (solid red = charging, green = full).
// Kept independent of Win32 so it can be tested without the controller.
#include <algorithm>
#include <deque>
#include <vector>

namespace ArmorX {

class Trend {
public:
    static constexpr int kSmoothWindow = 5;  // median over the last N raw readings
    static constexpr int kTrendWindow = 4;   // smoothed readings compared to judge the trend
    static constexpr int kRiseToCharge = 2;  // smoothed % must rise this much over kTrendWindow
    static constexpr int kFull = 99;         // at or above this, never "charging"

    // Adds one raw reading; returns the smoothed level and sets `charging`.
    int Add(int percent, bool& charging) {
        raw_.push_back(percent);
        if ((int)raw_.size() > kSmoothWindow) raw_.pop_front();
        std::vector<int> v(raw_.begin(), raw_.end());
        std::sort(v.begin(), v.end());
        size_t n = v.size();
        int level = n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2] + 1) / 2;
        smooth_.push_back(level);
        if ((int)smooth_.size() > kTrendWindow) smooth_.pop_front();
        charging = level < kFull && (int)smooth_.size() == kTrendWindow &&
                   smooth_.back() - smooth_.front() >= kRiseToCharge;
        return level;
    }

private:
    std::deque<int> raw_, smooth_;
};

}  // namespace ArmorX
