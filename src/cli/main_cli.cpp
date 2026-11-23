// Minimal command-line harness for the macOS port: runs one poll and prints what the vendor
// drivers found. Used to validate the IOKit HID backend before the SwiftUI menu-bar app exists.
#include <cstdio>

#include "../devices.h"
#include "../platform_compat.h"

static const char* KindName(DeviceKind k) {
    switch (k) {
        case DeviceKind::Mouse: return "mouse";
        case DeviceKind::Headset: return "headset";
        case DeviceKind::Keyboard: return "keyboard";
        case DeviceKind::Gamepad: return "gamepad";
        case DeviceKind::Phone: return "phone";
        case DeviceKind::Microphone: return "mic";
        case DeviceKind::Speaker: return "speaker";
        default: return "other";
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--dump") {
        printf("%s", WideToUtf8(DumpHidCollections()).c_str());
        return 0;
    }
    auto devs = PollAllDevices();
    printf("Found %zu device(s):\n", devs.size());
    for (const auto& d : devs) {
        std::string pct = d.percent >= 0 ? std::to_string(d.percent) + "%" : "?";
        printf("  [%4s] %-28s via %-18s", pct.c_str(),
               WideToUtf8(d.name).c_str(), WideToUtf8(d.via).c_str());
        printf(" %-8s", KindName(d.kind));
        if (d.charging) printf(" charging");
        if (d.full) printf(" full");
        if (!d.detail.empty()) printf(" | %s", WideToUtf8(d.detail).c_str());
        printf("  [id=%s]", WideToUtf8(d.key).c_str());
        printf("\n");
    }
    return 0;
}
