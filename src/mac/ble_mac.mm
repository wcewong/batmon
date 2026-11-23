// macOS Bluetooth / Apple battery sources. See ble_mac.h.
//
// Three sources, merged by device name (fresh value wins):
//   * CoreBluetooth standard Battery Service (0x180F / 0x2A19) — the authoritative, live reading for
//     standard BLE devices (keyboards, knobs, earbuds). Connecting to a system-owned peripheral
//     takes a few seconds, so reads are bounded by a timeout.
//   * `system_profiler SPBluetoothDataType -json` — names, device kinds, and cached levels; also
//     keeps a peripheral visible (level unknown) right after waking, before CoreBluetooth re-reads.
//   * IORegistry "BatteryPercent" — Apple Magic Mouse/Trackpad/Keyboard.
#include "ble_mac.h"

#import <Foundation/Foundation.h>
#include <IOKit/IOKitLib.h>

#include <cctype>
#include <cstdio>
#include <map>

namespace {

bool DebugOn() { static bool on = getenv("BATMON_DEBUG") != nullptr; return on; }

std::wstring CFToW(CFStringRef s) {
    if (!s) return {};
    CFIndex n = CFStringGetLength(s);
    std::wstring out;
    out.reserve(n);
    for (CFIndex i = 0; i < n; i++) out.push_back((wchar_t)CFStringGetCharacterAtIndex(s, i));
    return out;
}
std::wstring NSToW(NSString* s) { return s ? CFToW((__bridge CFStringRef)s) : std::wstring(); }

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower((wint_t)c);
    return s;
}

int KindFromMinorType(NSString* t) {
    if (![t isKindOfClass:[NSString class]]) return -1;
    NSString* s = t.lowercaseString;
    if ([s containsString:@"mouse"] || [s containsString:@"trackpad"] ||
        [s containsString:@"pointing"]) return 1;   // Mouse
    if ([s containsString:@"headphone"] || [s containsString:@"headset"] ||
        [s containsString:@"earbud"] || [s containsString:@"audio"]) return 2;  // Headset
    if ([s containsString:@"keyboard"]) return 3;   // Keyboard
    if ([s containsString:@"gamepad"] || [s containsString:@"controller"] ||
        [s containsString:@"joystick"]) return 4;   // Gamepad
    if ([s containsString:@"speaker"]) return 7;    // Speaker
    if ([s containsString:@"microphone"]) return 6; // Microphone
    return -1;
}

// ---- IORegistry BatteryPercent scraper (Apple Magic devices) ---------------------------------

void ScrapeIORegistry(std::vector<MacBattery>& out) {
    io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
    if (!root) return;
    io_iterator_t it = 0;
    if (IORegistryEntryCreateIterator(root, kIOServicePlane,
                                      kIORegistryIterateRecursively, &it) != KERN_SUCCESS)
        return;
    for (io_registry_entry_t e = IOIteratorNext(it); e; e = IOIteratorNext(it)) {
        CFTypeRef pct = IORegistryEntryCreateCFProperty(e, CFSTR("BatteryPercent"), kCFAllocatorDefault, 0);
        if (pct && CFGetTypeID(pct) == CFNumberGetTypeID()) {
            int v = -1;
            CFNumberGetValue((CFNumberRef)pct, kCFNumberIntType, &v);
            MacBattery b;
            b.percent = v;
            b.via = L"Bluetooth";
            CFTypeRef name = IORegistryEntryCreateCFProperty(e, CFSTR("Product"), kCFAllocatorDefault, 0);
            if (name && CFGetTypeID(name) == CFStringGetTypeID()) b.name = CFToW((CFStringRef)name);
            if (name) CFRelease(name);
            CFTypeRef addr = IORegistryEntryCreateCFProperty(e, CFSTR("DeviceAddress"), kCFAllocatorDefault, 0);
            if (addr && CFGetTypeID(addr) == CFStringGetTypeID()) b.address = CFToW((CFStringRef)addr);
            if (addr) CFRelease(addr);
            if (!b.name.empty()) out.push_back(b);
        }
        if (pct) CFRelease(pct);
        IOObjectRelease(e);
    }
    IOObjectRelease(it);
}

// ---- system_profiler SPBluetoothDataType parser ----------------------------------------------

void ParseDeviceEntry(NSDictionary* entry, std::vector<MacBattery>& out) {
    for (NSString* name in entry) {
        id props = entry[name];
        if (![props isKindOfClass:[NSDictionary class]]) continue;
        int kind = KindFromMinorType(props[@"device_minorType"]);
        id lvl = props[@"device_batteryLevelMain"];
        bool hasBattery = [lvl isKindOfClass:[NSString class]];
        // Keep known battery peripherals visible even when the system has no current level (e.g.
        // just after waking, before CoreBluetooth re-reads).
        if (!hasBattery && kind < 0) continue;
        MacBattery b;
        b.name = NSToW(name);
        b.percent = hasBattery ? [(NSString*)lvl intValue] : -1;
        b.via = L"Bluetooth";
        b.kind = kind;
        id addr = props[@"device_address"];
        if ([addr isKindOfClass:[NSString class]]) b.address = NSToW(addr);
        out.push_back(b);
    }
}

void ScrapeSystemProfiler(std::vector<MacBattery>& out) {
    @autoreleasepool {
        NSTask* task = [[NSTask alloc] init];
        task.launchPath = @"/usr/sbin/system_profiler";
        task.arguments = @[@"SPBluetoothDataType", @"-json"];
        NSPipe* pipe = [NSPipe pipe];
        task.standardOutput = pipe;
        task.standardError = [NSPipe pipe];
        @try { [task launch]; } @catch (NSException* ex) { return; }
        NSData* data = [pipe.fileHandleForReading readDataToEndOfFile];
        [task waitUntilExit];
        if (!data.length) return;
        id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if (![root isKindOfClass:[NSDictionary class]]) return;
        id arr = root[@"SPBluetoothDataType"];
        if (![arr isKindOfClass:[NSArray class]]) return;
        for (id el in arr) {
            if (![el isKindOfClass:[NSDictionary class]]) continue;
            id connected = el[@"device_connected"];
            if ([connected isKindOfClass:[NSArray class]])
                for (id entry in connected)
                    if ([entry isKindOfClass:[NSDictionary class]]) ParseDeviceEntry(entry, out);
        }
    }
}

}  // namespace

// ---- public entry ---------------------------------------------------------------------------
// Note: the live BLE battery read (CoreBluetooth GATT 0x180F) is done in Swift (BLEReader), driven
// by the app's main run loop, because CoreBluetooth delivers its callbacks as main-queue blocks and
// cannot be read synchronously from this background poll without starving that queue. Here we only
// provide the cached/visible set; Swift merges the fresh BLE percentages on top.

std::vector<MacBattery> MacPollBluetoothBatteries(int /*timeoutMs*/) {
    std::vector<MacBattery> sp;
    ScrapeSystemProfiler(sp);   // names, kinds, cached levels; keeps a device visible after wake
    ScrapeIORegistry(sp);       // Apple Magic devices

    // Merge by lowercased name/address: first-seen wins for identity/kind, a known percent beats -1.
    std::map<std::wstring, MacBattery> merged;
    for (const auto& b : sp) {
        std::wstring key = Lower(b.name.empty() ? b.address : b.name);
        if (key.empty()) continue;
        auto& m = merged[key];
        if (m.name.empty()) m = b;
        if (b.address.size() && m.address.empty()) m.address = b.address;
        if (b.kind >= 0 && m.kind < 0) m.kind = b.kind;
        if (b.percent >= 0 && m.percent < 0) m.percent = b.percent;
    }
    std::vector<MacBattery> out;
    for (auto& [k, v] : merged) out.push_back(v);
    return out;
}
