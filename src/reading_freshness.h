#pragma once
// Battery reading age policy (PR #2143). Only an actual, connected, non-sleeping percentage sample updates
// the timestamp; missing, offline or cached samples are not fresh data. The thresholds grow with the poll
// interval, so a long "Check battery every" choice does not make every reading look stale between polls.
// Portable C++17: no Windows APIs and no I/O.
#include <algorithm>
#include <cstdint>

namespace reading_freshness {

constexpr std::uint64_t kDelayedAfterMs = 3ULL * 60 * 1000;
constexpr std::uint64_t kStaleAfterMs = 10ULL * 60 * 1000;

enum class State { Unknown, Fresh, Delayed, Stale };

inline bool Confirmed(int percent, bool connected, bool asleep) {
    return connected && !asleep && percent >= 0 && percent <= 100;
}

// GetTickCount64() is monotonic across wall-clock changes. Zero means never read.
inline std::uint64_t AgeMs(std::uint64_t lastTick, std::uint64_t now) {
    return !lastTick || now < lastTick ? 0 : now - lastTick;
}

inline std::uint64_t AgeMinutes(std::uint64_t lastTick, std::uint64_t now) {
    return AgeMs(lastTick, now) / (60ULL * 1000);
}

// Delayed after two missed polls (at least 3 min), stale after three (at least 10 min).
inline State Classify(std::uint64_t lastTick, std::uint64_t now, std::uint64_t pollMs) {
    if (!lastTick) return State::Unknown;
    const auto age = AgeMs(lastTick, now);
    if (age >= std::max(kStaleAfterMs, 3 * pollMs)) return State::Stale;
    if (age >= std::max(kDelayedAfterMs, 2 * pollMs)) return State::Delayed;
    return State::Fresh;
}

}  // namespace reading_freshness
