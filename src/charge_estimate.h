#pragma once
// Pure, portable time-to-full estimate from the readings seen during the current charge (one per poll).
// No HID access, Windows dependencies, timers or disk writes. Like the time-to-empty estimate it is an
// approximation from observed readings, not a device-reported value: the last few percent often charge
// slower, so the UI shows it as "~40 min".
#include <cstdint>

namespace charge {

struct Tracker {
    int64_t startTime = 0;  // when this charge was first seen (seconds)
    int startPercent = -1;  // level at that moment; -1 = not charging
    int lastPercent = -1;
};

// Feed every fresh reading. A new charge starts when charging begins, or when the level goes down while
// charging (the device was used meanwhile, the rate seen so far no longer applies).
inline void Observe(Tracker& t, bool charging, int percent, int64_t now) {
    if (!charging || percent < 0 || percent > 100) { t = Tracker{}; return; }
    if (t.startPercent < 0 || percent < t.lastPercent) {
        t.startTime = now;
        t.startPercent = percent;
    }
    t.lastPercent = percent;
}

// Minutes until 100%, or -1 when there is no reliable estimate yet: at least 10 minutes and 3 percentage
// points of rise this charge, and a plausible rate (0.1 to 5 points a minute). 0 when already full.
inline int MinutesToFull(const Tracker& t, int64_t now) {
    if (t.startPercent < 0 || t.lastPercent < 0) return -1;
    if (t.lastPercent >= 100) return 0;
    const int64_t elapsed = now - t.startTime;
    const int rise = t.lastPercent - t.startPercent;
    if (elapsed < 10 * 60 || rise < 3) return -1;
    const double perMinute = rise * 60.0 / static_cast<double>(elapsed);
    if (perMinute < 0.1 || perMinute > 5.0) return -1;
    const int minutes = static_cast<int>((100 - t.lastPercent) / perMinute + 0.5);
    return minutes > 24 * 60 ? -1 : minutes;
}

// A charge-today warning is due when the time-to-empty is short, but not yet low enough for the low-battery
// alert: once per discharge, re-armed when the device charges.
constexpr double kShortRuntimeHours = 12.0;
inline bool ShouldWarnShortRuntime(double hoursRemaining, bool charging, int percent, int lowThreshold, bool& warned) {
    if (charging) { warned = false; return false; }
    if (warned || hoursRemaining <= 0 || hoursRemaining >= kShortRuntimeHours || percent < 0) return false;
    if (lowThreshold >= 0 && percent <= lowThreshold) return false;  // the low-battery alert covers it
    warned = true;
    return true;
}

}  // namespace charge
