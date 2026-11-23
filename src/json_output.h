#pragma once
// Machine-readable device list, shared by the Windows and Linux entry points.
//
// `--json` prints one JSON array of the devices found in a single poll. The
// fields are deliberately stable so external tools (bar widgets, scripts) can
// rely on them; adding a field is backwards compatible.
//
// Header-only so neither Makefile needs a new translation unit.
#include <cstdio>
#include <string>
#include <vector>

#include "devices.h"
#include "platform_compat.h"

inline std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;  // UTF-8 bytes pass through unchanged
        }
    }
    return o;
}

inline const char* DeviceKindName(DeviceKind k) {
    switch (k) {
        case DeviceKind::Mouse:    return "mouse";
        case DeviceKind::Headset:  return "headset";
        case DeviceKind::Keyboard: return "keyboard";
        case DeviceKind::Gamepad:  return "gamepad";
        case DeviceKind::Phone:    return "phone";
        default:                   return "other";
    }
}

inline std::string DevicesToJson(const std::vector<DeviceStatus>& list) {
    std::string out = "[";
    bool first = true;
    for (const auto& s : list) {
        if (!first) out += ',';
        first = false;
        out += "{\"key\":\"" + JsonEscape(WideToUtf8(s.key)) + "\""
               ",\"name\":\"" + JsonEscape(WideToUtf8(s.name)) + "\""
               ",\"via\":\"" + JsonEscape(WideToUtf8(s.via)) + "\""
               ",\"kind\":\"" + DeviceKindName(s.kind) + "\""
               ",\"percent\":" + std::to_string(s.percent) +
               ",\"level\":\"" + JsonEscape(WideToUtf8(s.levelText)) + "\""
               ",\"detail\":\"" + JsonEscape(WideToUtf8(s.detail)) + "\""
               ",\"charging\":" + (s.charging ? "true" : "false") +
               ",\"asleep\":" + (s.asleep ? "true" : "false") +
               ",\"connected\":" + (s.connected ? "true" : "false") +
               ",\"millivolts\":" + std::to_string(s.millivolts) +
               ",\"graceSec\":" + std::to_string(s.graceSec) + "}";
    }
    out += "]\n";
    return out;
}
