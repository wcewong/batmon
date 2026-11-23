// macOS driver orchestration (adapted from the upstream Linux devices_linux.cpp).
//
// Deliberately separate from src/devices.cpp so the Windows build is untouched. This phase enables
// the well-known vendors whose drivers only need the portable HID API. Add more Poll* calls here
// (and the matching .cpp to Makefile.mac) in later phases.
#include "../devices.h"

#include <cstdio>
#include <cwctype>
#include <set>

#include "../drivers/drivers.h"
#include "../hid.h"
#include "ble_mac.h"

std::vector<DeviceStatus> PollAllDevices() {
    PollContext ctx;
    ctx.colls = EnumHid();

    // Phase 1: famous vendors, HID only.
    PollZebronics(ctx);      // Compx 3554:FC0A before Kysona, which also scans VID 3554
    PollGlorious(ctx);       // also G-Wolves / Hitscan (defines the shared G-Wolves model table)
    PollKysona(ctx);         // also Pulsar, ATK/VXE and other popular Chinese mice
    PollKeychron(ctx);       // Keychron mice and trackballs
    PollLogitech(ctx);       // Unifying / Bolt / Lightspeed / Bluetooth HID++
    PollRazer(ctx);
    PollSteelSeries(ctx);
    PollCorsair(ctx);
    PollHyperX(ctx);
    PollPlayStation(ctx);    // DualShock 4 / DualSense over USB or Bluetooth HID
    PollCoolerMaster(ctx);
    PollFinalmouse(ctx);     // UltralightX / Starlight X dongles
    PollLamzu(ctx);          // LAMZU wireless mice
    PollAula(ctx);           // AULA / MCHOSE wireless keyboards
    PollNinjutso(ctx);       // Sora V2 / Ten on their 1K receivers
    PollRapoo(ctx);          // Rapoo VT gaming mice (passive status reports)

    // macOS-native battery sources: Apple Magic devices (IORegistry) and Bluetooth devices
    // (system_profiler), for anything the vendor HID drivers above did not already report.
    auto lower = [](std::wstring s) {
        for (auto& c : s) c = (wchar_t)towlower((wint_t)c);
        return s;
    };
    // CoreBluetooth connect+read of a system-owned peripheral can take several seconds; give it room.
    for (const auto& b : MacPollBluetoothBatteries(30000)) {
        // Dedup against HID devices by fuzzy name: Bluetooth reports "LIFT", the HID driver reports
        // "Logitech LIFT VERTICAL ERGONOMIC MOUSE" — treat one being a substring of the other (>=4
        // chars) as the same device, keeping the richer HID entry.
        std::wstring lb = lower(b.name);
        bool known = false;
        for (const auto& s : ctx.out) {
            std::wstring ls = lower(s.name);
            if (ls.empty() || lb.empty()) continue;
            if ((lb.size() >= 4 && ls.find(lb) != std::wstring::npos) ||
                (ls.size() >= 4 && lb.find(ls) != std::wstring::npos)) { known = true; break; }
        }
        if (known) continue;
        DeviceStatus s;
        s.name = b.name;
        s.key = ctx.StableKey(b.address.empty() ? b.name : (L"bt:" + b.address));
        s.via = b.via;
        s.percent = b.percent;
        s.kind = b.kind >= 0 ? (DeviceKind)b.kind : GuessKind(b.name);
        s.connected = true;
        ctx.out.push_back(s);
    }

    // Collapse duplicate reports of the same device (e.g. a Logitech receiver can answer for the
    // same unit across several of its collections): a device key is a stable unique identity.
    std::set<std::wstring> seenKeys;
    std::vector<DeviceStatus> uniq;
    uniq.reserve(ctx.out.size());
    for (auto& s : ctx.out) {
        if (!s.key.empty() && !seenKeys.insert(s.key).second) continue;
        uniq.push_back(std::move(s));
    }
    return uniq;
}

std::wstring DumpHidCollections() {
    std::wstring out = L"VID:PID    IF  UsagePg:Usage  In  Out Feat  Product\n";
    for (const auto& c : EnumHid()) {
        wchar_t line[256];
        swprintf(line, 256, L"%04X:%04X  %2d  %04X:%04X    %3u %3u %3u   ", c.vid, c.pid, c.iface,
                 c.page(), c.usage(), c.inLen(), c.outLen(), c.featLen());
        out += line + HidProduct(c) + L"\n";
    }
    return out;
}

// No persistent background driver sessions yet on macOS (AAP/BLE channels arrive in a later phase).
void ShutdownDevices() {}
