#pragma once
// A mouse moved between dongle and cable answers under two ids with different names (SCYROX 8K
// dongle, SCYROX V6 cable). While a Kysona device is live, the ids that went silent or answered
// "not connected" are reported as off, so the tray hides them instead of holding them dimmed as
// asleep. shortcut: also hides a second, sleeping Kysona mouse.
#include "../devices.h"

#include <map>

using KysonaEntries = std::map<std::wstring, DeviceStatus>;

// live: added this poll. off: answered "not connected" this poll. lastLive: live last poll, updated here.
inline void HideSilentKysona(const KysonaEntries& live, KysonaEntries off, KysonaEntries& lastLive,
                             std::vector<DeviceStatus>& out) {
    if (live.empty()) return;  // nothing live: keep holding the last values (the mouse sleeps)
    for (auto& [key, s] : lastLive)
        if (!live.count(key)) off.emplace(key, s);
    for (auto& [key, s] : off) {
        if (live.count(key)) continue;
        s.connected = false;
        s.percent = -1;
        s.charging = false;
        out.push_back(s);
    }
    lastLive = live;
}
