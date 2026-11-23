// macOS HID backend for the shared vendor drivers.
//
// Implements the hid.h interface (EnumHid + HidDev) on top of IOKit's IOHIDManager, so every
// portable vendor driver in src/drivers/ compiles and runs unchanged — exactly as the Linux
// hidraw backend (hid_linux.cpp) does. The report-buffer conventions match Windows/hidapi:
//   * Write/SetFeature: data[0] is the report ID (0 for unnumbered), zero-padded to report length.
//   * Read: a leading zero report ID is stripped, so data[0] is the first payload byte for
//     unnumbered reports and the report ID for numbered ones.
//
// IOKit parses the report descriptor for us (usage pairs + max report sizes), so this backend is
// shorter than the Linux one: no manual descriptor parser is needed.

#ifndef __APPLE__
// Intentionally empty on other platforms.
#else

#include "../hid.h"

#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <deque>
#include <map>
#include <set>

namespace {

bool DebugOn() { static bool on = getenv("BATMON_DEBUG") != nullptr; return on; }

// Input reports are delivered on whatever run loop the device is scheduled on. Rather than run a
// shared background run loop (which makes callback/teardown lifetime racy across threads), each
// HidDev schedules its device on the *calling* thread's run loop and pumps that run loop inline
// during Read(). PollAllDevices() runs every Open/Read/Close for a device on one synchronous
// thread, so everything is single-threaded and there is no cross-thread use-after-free.

// ---- IOHIDManager + device registry ----------------------------------------------------------

IOHIDManagerRef gManager = nullptr;
// Registry entry id -> device ref (owned by the retained gDeviceSet). Rebuilt each EnumHid.
std::map<uint64_t, IOHIDDeviceRef> gDevices;
CFSetRef gDeviceSet = nullptr;

IOHIDManagerRef EnsureManager() {
    if (gManager) return gManager;
    gManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
    if (!gManager) return nullptr;
    IOHIDManagerSetDeviceMatching(gManager, nullptr);  // match everything
    if (IOHIDManagerOpen(gManager, kIOHIDManagerOptionNone) != kIOReturnSuccess) {
        // Open can fail without Input Monitoring permission; enumeration then yields nothing.
    }
    return gManager;
}

int IntProp(IOHIDDeviceRef d, CFStringRef key) {
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    int n = 0;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID()) CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, &n);
    return n;
}

std::wstring StrProp(IOHIDDeviceRef d, CFStringRef key) {
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return {};
    CFStringRef s = (CFStringRef)v;
    CFIndex len = CFStringGetLength(s);
    std::wstring out;
    out.reserve(len);
    for (CFIndex i = 0; i < len; i++) out.push_back((wchar_t)CFStringGetCharacterAtIndex(s, i));
    return out;
}

uint64_t EntryId(IOHIDDeviceRef d) {
    io_service_t svc = IOHIDDeviceGetService(d);
    uint64_t id = 0;
    if (svc) IORegistryEntryGetRegistryEntryID(svc, &id);
    return id;
}

// USB interface number (kUSBInterfaceNumber), -1 when not a USB interface.
int InterfaceOf(IOHIDDeviceRef d) {
    CFTypeRef v = IOHIDDeviceGetProperty(d, CFSTR("InterfaceNumber"));
    if (!v) v = IOHIDDeviceGetProperty(d, CFSTR("bInterfaceNumber"));
    int n = -1;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID()) CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, &n);
    return n;
}

}  // namespace

std::vector<HidColl> EnumHid() {
    std::vector<HidColl> out;
    IOHIDManagerRef mgr = EnsureManager();
    if (!mgr) return out;

    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set) return out;

    // Swap the retained device set so HidDev::Open can resolve refs after this call returns.
    if (gDeviceSet) CFRelease(gDeviceSet);
    gDeviceSet = set;  // keep retained
    gDevices.clear();

    CFIndex n = CFSetGetCount(set);
    std::vector<const void*> devs(n);
    CFSetGetValues(set, devs.data());

    for (CFIndex i = 0; i < n; i++) {
        IOHIDDeviceRef d = (IOHIDDeviceRef)devs[i];
        uint64_t id = EntryId(d);
        if (!id) continue;
        gDevices[id] = d;

        HidColl base;
        wchar_t pathbuf[32];
        swprintf(pathbuf, 32, L"%llu", (unsigned long long)id);
        base.path = pathbuf;
        base.vid = (USHORT)IntProp(d, CFSTR(kIOHIDVendorIDKey));
        base.pid = (USHORT)IntProp(d, CFSTR(kIOHIDProductIDKey));
        base.version = (USHORT)IntProp(d, CFSTR(kIOHIDVersionNumberKey));
        base.iface = InterfaceOf(d);
        base.product = StrProp(d, CFSTR(kIOHIDProductKey));
        USHORT inLen = (USHORT)IntProp(d, CFSTR(kIOHIDMaxInputReportSizeKey));
        USHORT outLen = (USHORT)IntProp(d, CFSTR(kIOHIDMaxOutputReportSizeKey));
        USHORT featLen = (USHORT)IntProp(d, CFSTR(kIOHIDMaxFeatureReportSizeKey));

        // One HidColl per top-level usage pair (Windows/Linux emit one per application collection),
        // so the drivers' page/usage filters behave the same. Report lengths are device-wide maxima
        // on macOS; that is a safe upper bound for the zero-padded buffers the drivers use.
        CFTypeRef pairs = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDDeviceUsagePairsKey));
        auto emit = [&](USHORT page, USHORT usage) {
            HidColl c = base;
            c.caps.UsagePage = page;
            c.caps.Usage = usage;
            c.caps.InputReportByteLength = inLen;
            c.caps.OutputReportByteLength = outLen;
            c.caps.FeatureReportByteLength = featLen;
            out.push_back(std::move(c));
        };
        if (pairs && CFGetTypeID(pairs) == CFArrayGetTypeID() && CFArrayGetCount((CFArrayRef)pairs) > 0) {
            CFArrayRef arr = (CFArrayRef)pairs;
            for (CFIndex k = 0; k < CFArrayGetCount(arr); k++) {
                CFDictionaryRef pd = (CFDictionaryRef)CFArrayGetValueAtIndex(arr, k);
                int pg = 0, us = 0;
                CFNumberRef pgn = (CFNumberRef)CFDictionaryGetValue(pd, CFSTR(kIOHIDDeviceUsagePageKey));
                CFNumberRef usn = (CFNumberRef)CFDictionaryGetValue(pd, CFSTR(kIOHIDDeviceUsageKey));
                if (pgn) CFNumberGetValue(pgn, kCFNumberIntType, &pg);
                if (usn) CFNumberGetValue(usn, kCFNumberIntType, &us);
                emit((USHORT)pg, (USHORT)us);
            }
        } else {
            emit((USHORT)IntProp(d, CFSTR(kIOHIDPrimaryUsagePageKey)),
                 (USHORT)IntProp(d, CFSTR(kIOHIDPrimaryUsageKey)));
        }
    }
    return out;
}

// ---- HidDev ----------------------------------------------------------------------------------
// Per-device input-report queue, fed by the shared run loop, drained by Read().

struct HidDev::Impl {
    IOHIDDeviceRef dev = nullptr;
    CFRunLoopRef runLoop = nullptr;          // the thread's run loop the device is scheduled on
    std::vector<uint8_t> inbuf;              // callback scratch, sized to inLen
    std::deque<std::vector<BYTE>> queue;     // received input reports
    bool scheduled = false;
};

// Impls that currently have a registered callback. Open/Close and the callback all run on the one
// poll thread, so this needs no locking. The callback consults it to reject a report that IOKit
// dispatches for an Impl that was already closed (a stale mach message) instead of dereferencing it.
static std::set<void*> gLiveImpls;

namespace {
// Runs on the same thread that pumps the run loop in Read(), so no locking is needed.
void InputReportCb(void* ctx, IOReturn, void*, IOHIDReportType, uint32_t reportID,
                   uint8_t* report, CFIndex len) {
    if (!gLiveImpls.count(ctx)) return;  // stale delivery for a closed device
    auto* im = (HidDev::Impl*)ctx;
    // IOKit's callback buffer already starts with the report-ID byte for numbered reports (and the
    // same id is passed separately). Use it as-is to satisfy the hid.h Read contract: data[0] is the
    // report ID for numbered reports, the first payload byte for unnumbered ones (IOKit omits the id
    // there). Prepending the id again would duplicate it and shift every field by one byte.
    size_t off = (reportID == 0 && len > 0 && report[0] == 0) ? 1 : 0;  // strip a stray leading 0
    im->queue.emplace_back(report + off, report + len);
}
}  // namespace

static IOHIDDeviceRef ResolveDevice(const HidColl& c) {
    uint64_t id = 0;
    swscanf(c.path.c_str(), L"%llu", &id);
    auto it = gDevices.find(id);
    return it == gDevices.end() ? nullptr : it->second;
}

bool HidDev::Open(const HidColl& c) {
    Close();
    c_ = c;
    IOHIDDeviceRef d = ResolveDevice(c);
    if (!d) return false;
    if (getenv("BATMON_DEBUG"))
        fprintf(stderr, "[hid] Open attempt %04X:%04X page=%04X usage=%04X iface=%d\n", c.vid, c.pid,
                c.caps.UsagePage, c.caps.Usage, c.iface);
    IOReturn r = IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone);
    if (r != kIOReturnSuccess) {
        if (getenv("BATMON_DEBUG"))
            fprintf(stderr, "[hid] Open %04X:%04X page=%04X failed: 0x%08X\n", c.vid, c.pid,
                    c.caps.UsagePage, r);
        return false;
    }

    impl_ = new Impl();
    impl_->dev = d;
    size_t cap = c.inLen() ? c.inLen() : 64;
    impl_->inbuf.assign(cap, 0);
    // Schedule on the calling thread's run loop. Reads pump this same run loop, and Close() runs on
    // this same thread, so the input callback can never fire concurrently with teardown.
    impl_->runLoop = CFRunLoopGetCurrent();
    gLiveImpls.insert(impl_);
    IOHIDDeviceScheduleWithRunLoop(d, impl_->runLoop, kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(d, impl_->inbuf.data(), (CFIndex)impl_->inbuf.size(),
                                           InputReportCb, impl_);
    impl_->scheduled = true;
    return true;
}

bool HidDev::OpenReadOnly(const HidColl& c) { return Open(c); }
bool HidDev::OpenFeatureOnly(const HidColl& c) { return Open(c); }

void HidDev::Close() {
    if (!impl_) return;
    IOHIDDeviceRef d = impl_->dev;
    if (d && impl_->scheduled) {
        // Drain any input reports IOKit has already posted to this device's mach port *while impl_ is
        // still valid*. Otherwise a report queued before Close can be dispatched on a later run-loop
        // pump (e.g. the next device's Read), invoking InputReportCb with a freed Impl -> SIGSEGV.
        while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) == kCFRunLoopRunHandledSource) {}
        // Now stop callbacks and remove the source before freeing the buffer/Impl they reference.
        IOHIDDeviceRegisterInputReportCallback(d, impl_->inbuf.data(), (CFIndex)impl_->inbuf.size(),
                                               nullptr, nullptr);
        IOHIDDeviceUnscheduleFromRunLoop(d, impl_->runLoop, kCFRunLoopDefaultMode);
    }
    if (d) IOHIDDeviceClose(d, kIOHIDOptionsTypeNone);
    gLiveImpls.erase(impl_);
    delete impl_;
    impl_ = nullptr;
}

bool HidDev::ok() const { return impl_ && impl_->dev; }

// Splits off the report-id byte the way hidapi's Darwin backend does: a 0 id (unnumbered report)
// is not transmitted, so skip the leading byte.
static IOReturn SendReport(IOHIDDeviceRef d, IOHIDReportType type, const std::vector<BYTE>& data) {
    if (data.empty()) return kIOReturnBadArgument;
    uint32_t reportID = data[0];
    const uint8_t* bytes = data.data();
    CFIndex len = (CFIndex)data.size();
    if (reportID == 0) { bytes++; len--; }
    if (len <= 0) return kIOReturnBadArgument;
    return IOHIDDeviceSetReport(d, type, reportID, bytes, len);
}

bool HidDev::Write(const std::vector<BYTE>& data, int timeoutMs) {
    (void)timeoutMs;
    if (!ok() || !c_.outLen() || data.empty()) return false;
    std::vector<BYTE> buf(c_.outLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    IOReturn r = SendReport(impl_->dev, kIOHIDReportTypeOutput, buf);
    if (DebugOn()) fprintf(stderr, "[hid] Write out %zuB -> 0x%08X\n", buf.size(), r);
    return r == kIOReturnSuccess;
}

int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    if (!ok()) return -1;
    // Pump this thread's run loop so IOKit delivers input reports into impl_->queue (the callback
    // runs inline, on this thread). Loop until a report lands or the deadline passes.
    CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + (timeoutMs > 0 ? timeoutMs / 1000.0 : 0);
    while (impl_->queue.empty()) {
        CFAbsoluteTime remaining = deadline - CFAbsoluteTimeGetCurrent();
        if (remaining <= 0) break;
        // Returns after one source is handled (a report) or the timeout elapses.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, remaining, true);
    }
    if (impl_->queue.empty()) {
        if (DebugOn()) fprintf(stderr, "[hid] Read timeout (%dms)\n", timeoutMs);
        return -1;
    }
    buf = std::move(impl_->queue.front());
    impl_->queue.pop_front();
    if (DebugOn()) {
        fprintf(stderr, "[hid] Read got %zuB:", buf.size());
        for (size_t i = 0; i < buf.size() && i < 12; i++) fprintf(stderr, " %02X", buf[i]);
        fprintf(stderr, "\n");
    }
    return (int)buf.size();
}

int HidDev::WriteAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& buf, int timeoutMs,
                         bool* writeSucceeded) {
    Flush();
    bool w = Write(data, timeoutMs);
    if (writeSucceeded) *writeSucceeded = w;
    if (!w) return -1;
    return Read(buf, timeoutMs);
}

int HidDev::SetOutputAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& buf, int timeoutMs) {
    if (!ok() || !c_.outLen() || data.empty()) return -1;
    Flush();
    std::vector<BYTE> out(c_.outLen(), 0);
    memcpy(out.data(), data.data(), std::min(out.size(), data.size()));
    if (SendReport(impl_->dev, kIOHIDReportTypeOutput, out) != kIOReturnSuccess) return -1;
    return Read(buf, timeoutMs);
}

void HidDev::Flush() {
    if (!ok()) return;
    // Drain any reports IOKit has already queued, then discard everything buffered.
    while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) == kCFRunLoopRunHandledSource) {}
    impl_->queue.clear();
}

bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    if (!ok() || !c_.featLen() || data.empty()) return false;
    std::vector<BYTE> buf(c_.featLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    return SendReport(impl_->dev, kIOHIDReportTypeFeature, buf) == kIOReturnSuccess;
}

bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    if (!ok() || !c_.featLen()) return false;
    out.assign(c_.featLen(), 0);
    out[0] = reportId;
    CFIndex len = (CFIndex)out.size() - 1;
    IOReturn r = IOHIDDeviceGetReport(impl_->dev, kIOHIDReportTypeFeature, reportId, out.data() + 1, &len);
    if (DebugOn()) fprintf(stderr, "[hid] GetFeature id=%u -> 0x%08X (%ldB)\n", reportId, r, (long)len);
    if (r != kIOReturnSuccess) return false;
    out.resize(len + 1);
    return true;
}

bool HidDev::GetFeatureByte(BYTE reportId, BYTE& value) {
    std::vector<BYTE> out;
    if (!GetFeature(reportId, out) || out.size() < 2 || out[0] != reportId) return false;
    value = out[1];
    return true;
}

bool HidDev::GetInputReport(BYTE reportId, std::vector<BYTE>& out) {
    if (!ok() || !c_.inLen()) return false;
    out.assign(c_.inLen(), 0);
    out[0] = reportId;
    CFIndex len = (CFIndex)out.size() - 1;
    if (IOHIDDeviceGetReport(impl_->dev, kIOHIDReportTypeInput, reportId, out.data() + 1, &len) !=
        kIOReturnSuccess)
        return false;
    out.resize(len + 1);
    return true;
}

// ---- pure helpers (identical behaviour to the Windows and Linux backends) ---------------------

std::wstring HidProduct(const HidColl& c) { return c.product; }

std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int Clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int MapTo100(int v, int lo, int hi) {
    if (hi <= lo) return Clamp100(v);
    return Clamp100((v - lo) * 100 / (hi - lo));
}

int Interpolate(int mv, const int* mvTable, const int* pctTable, int n) {
    if (mv >= mvTable[0]) return pctTable[0];
    for (int i = 1; i < n; i++) {
        if (mv >= mvTable[i]) {
            int span = mvTable[i - 1] - mvTable[i];
            return pctTable[i] + (pctTable[i - 1] - pctTable[i]) * (mv - mvTable[i]) / (span ? span : 1);
        }
    }
    return pctTable[n - 1];
}

static bool Contains(const std::wstring& hay, const wchar_t* needle) {
    return hay.find(needle) != std::wstring::npos;
}

DeviceKind GuessKind(const std::wstring& name) {
    std::wstring n;
    for (wchar_t ch : name) n.push_back((wchar_t)towlower((wint_t)ch));
    if (Contains(n, L"mouse") || Contains(n, L"mice") || Contains(n, L"trackball") ||
        Contains(n, L"trackpad")) return DeviceKind::Mouse;
    if (Contains(n, L"keyboard") || Contains(n, L"keys")) return DeviceKind::Keyboard;
    if (Contains(n, L"controller") || Contains(n, L"gamepad") || Contains(n, L"xbox") ||
        Contains(n, L"dualsense") || Contains(n, L"dualshock") || Contains(n, L"joy-con"))
        return DeviceKind::Gamepad;
    if (Contains(n, L"head") || Contains(n, L"buds") || Contains(n, L"pods") || Contains(n, L"ear") ||
        Contains(n, L"wh-") || Contains(n, L"wf-") || Contains(n, L"audio") || Contains(n, L"speaker") ||
        Contains(n, L"arctis") || Contains(n, L"cloud") || Contains(n, L"void") || Contains(n, L"virtuoso"))
        return DeviceKind::Headset;
    return DeviceKind::Other;
}

DeviceKind KindFromCollections(const std::vector<HidColl>& all, USHORT vid, USHORT pid) {
    bool mouse = false, pad = false, kbd = false;
    for (const auto& c : all) {
        if (c.vid != vid || c.pid != pid || (c.caps.UsagePage && c.caps.UsagePage != 0x01)) continue;
        if (!c.caps.Usage) continue;
        mouse |= c.caps.Usage == 0x02;
        pad |= c.caps.Usage == 0x04 || c.caps.Usage == 0x05;
        kbd |= c.caps.Usage == 0x06;
    }
    if (mouse) return DeviceKind::Mouse;
    if (pad) return DeviceKind::Gamepad;
    if (kbd) return DeviceKind::Keyboard;
    return DeviceKind::Other;
}

#endif  // __APPLE__
