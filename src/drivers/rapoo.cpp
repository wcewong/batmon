// Rapoo VT gaming mice. Battery is pushed periodically; no commands or handshake are needed.
// Sources:
//  * Official Rapoo Hub (https://hub.rapoo.cn/), VT_nrf54L input-report decoder
//    and hardware manifests: report 07 on FF00:0002, state at byte 7, percent at byte 8,
//    optional mouse PID at 13..14. Connection is the low nibble of byte 1 (0 RF / 1 BT / 2 USB).
//  * https://github.com/PyFlat/Device-Battery-Info (RapooProtocol.cs): older 7-byte BB B0
//    reports on FF00:0002, state at byte 5, percent at byte 6; charging can report 7F (unknown).
//  * VT1 Pro Max (receiver 1233, cable 4433): USBPcap captures from the owner in issue #136
//    show the same passive 7-byte report on interface 1 / FF00:0002, sent without any request:
//    receiver `BB B0 71 84 03 01 64` every ~3.2 s (Rapoo app: 100%), cable while charging
//    `BB B0 71 84 03 02 5F` (state 2 = charging, 95%).
//  * VT3 Pro Max (receiver 24AE:1244, issue #2258): same passive seven-byte BB B0
//    status reports on FF00:0002. Source: https://github.com/zbndev/rapoo_battery
// Legacy BB semantics are restricted to known product IDs; other Telink firmware differs.
//  * VT3 Max Gen 2 receiver 1417 on another firmware revision (issue #1396) exposes a 13-byte
//    FF00:0002 collection. Rapoo Hub's VT_nrf54L decoder reads report 07 at any length and only
//    takes the optional fields when present, so state/percent at bytes 7/8 still fit; the mouse
//    PID at 13..14 does not, so identity falls back to the receiver PID (same rapoo:4617 key).
#include "drivers.h"
#include "rapoo_protocol.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace {

// Selected names from Rapoo's manifests. Support is determined by the collection and valid
// status frame, so other models on the same protocol work without extending this table.
struct Model { USHORT cable, receiver; const wchar_t* name; };
const Model kModels[] = {
    { 0x4610, 0x1410, L"Rapoo VT0" },
    { 0x4620, 0x1420, L"Rapoo VT0 Max" },
    { 0x4615, 0x1415, L"Rapoo VT1" },
    { 0x4644, 0x1444, L"Rapoo VT1 Max" },
    { 0x4622, 0x1422, L"Rapoo VT2" },
    { 0x4623, 0x1423, L"Rapoo VT2 Max" },
    { 0x4612, 0x1412, L"Rapoo VT3" },
    { 0x4617, 0x1417, L"Rapoo VT3 Max (Gen 2)" },
    { 0x4B06, 0x1B06, L"Rapoo VT3 Max V2" },
    { 0x4611, 0x1411, L"Rapoo VT3S" },
    { 0x4621, 0x1421, L"Rapoo VT3S Max" },
    { 0x4613, 0x1413, L"Rapoo VT7" },
    { 0x4616, 0x1416, L"Rapoo VT7 Max" },
    { 0x4614, 0x1414, L"Rapoo VT9 (Gen 2)" },
    { 0x4618, 0x1418, L"Rapoo VT9 Max" },
    { 0x4415, 0x1215, L"Rapoo VT3 Pro" },
    { 0x4433, 0x1233, L"Rapoo VT1 Pro Max" },
};

const Model* FindModel(USHORT pid) {
    for (const auto& model : kModels)
        if (pid == model.cable || pid == model.receiver) return &model;
    return nullptr;
}

// Mice confirmed to send the 7-byte BB B0 status report.
bool IsLegacyVT3Pro(USHORT pid) {
    return pid == 0x1215 || pid == 0x4415 ||  // VT3 Pro
           pid == 0x1233 || pid == 0x4433 ||  // VT1 Pro Max (issue #136 captures)
           pid == 0x1244;                      // VT3 Pro Max receiver (issue #2258)
}

// Keep open handles between polls (like PollXenta) so Windows buffers passive status reports
// arriving during the 60 s refresh interval instead of missing mice whose push interval is >5 s.
std::map<std::wstring, std::unique_ptr<HidDev>> g_rapoo;  // by collection path

bool DrainQueued(HidDev& d, Rapoo::Battery& battery, Rapoo::Profile profile) {
    bool got = false;
    for (int i = 0; i < 64; i++) {
        std::vector<BYTE> r;
        if (d.Read(r, 30) < 0) break;
        if (Rapoo::DecodeBattery(r.data(), r.size(), battery, profile)) got = true;
    }
    return got;
}

bool WaitBattery(HidDev& d, Rapoo::Battery& battery, Rapoo::Profile profile, ULONGLONG end) {
    do {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        std::vector<BYTE> r;
        if (left <= 0 || d.Read(r, left) < 0) return false;
        if (Rapoo::DecodeBattery(r.data(), r.size(), battery, profile)) return true;
    } while (GetTickCount64() < end);
    return false;
}

bool ReadBattery(std::unique_ptr<HidDev>& slot, const HidColl& c, Rapoo::Battery& battery,
                 Rapoo::Profile profile, bool fresh) {
    if (!fresh && DrainQueued(*slot, battery, profile)) return true;
    // A passive report can take several seconds (VT1 Pro Max ~4 s; VT9 Gen 2 ~5-10 s on first open),
    // so wait up to 5 s when no queued packet was buffered yet. One deadline covers unrelated packets.
    ULONGLONG start = GetTickCount64();
    ULONGLONG end = start + 5000;
    if (WaitBattery(*slot, battery, profile, end)) return true;
    // If a persistent handle failed immediately (e.g. receiver reset between polls), reopen once.
    if (!fresh && GetTickCount64() - start < 50) {
        slot->Close();
        if (slot->Open(c)) return WaitBattery(*slot, battery, profile, end);
    }
    return false;
}

}  // namespace

void PollRapoo(PollContext& ctx) {
    std::set<std::wstring> present;
    for (auto [vid, pid] : ctx.Products(0x24AE)) {
        for (const HidColl* c : ctx.Find(vid, pid, [pid](const HidColl& c) {
                 // The other vendor collections are for configuration/firmware, not battery.
                 return c.page() == 0xFF00 && c.usage() == 0x0002 &&
                        (c.inLen() == 16 || c.inLen() == 19 || (c.inLen() == 13 && pid == 0x1417) ||
                         (c.inLen() == 7 && IsLegacyVT3Pro(pid)));
             })) {
            present.insert(c->path);
            auto& slot = g_rapoo[c->path];
            bool fresh = !slot;
            if (fresh) {
                slot = std::make_unique<HidDev>();
                if (!slot->Open(*c)) {
                    g_rapoo.erase(c->path);
                    continue;
                }
            }
            Rapoo::Battery battery;
            auto profile = c->inLen() == 7 ? Rapoo::Profile::LegacyVT3Pro : Rapoo::Profile::Nordic54L;
            if (!ReadBattery(slot, *c, battery, profile, fresh)) continue;

            const Model* model = FindModel(battery.mousePid ? battery.mousePid : pid);
            USHORT identity = model ? model->cable : (battery.mousePid ? battery.mousePid : pid);
            DeviceStatus s;
            s.key = L"rapoo:" + Hex4(identity);
            s.name = model ? model->name : HidProduct(*c);
            // No confirmed cable PID for the 1244 receiver; keep its own identity/key.
            if (pid == 0x1244) s.name = L"Rapoo VT3 Pro Max";
            if (s.name.empty() || s.name == L"Rapoo Gaming Device") s.name = L"Rapoo wireless mouse";
            s.kind = DeviceKind::Mouse;
            if (battery.connection == Rapoo::Connection::Cable ||
                (battery.connection == Rapoo::Connection::Unknown && model && pid == model->cable))
                s.via = L"USB cable";
            else if (battery.connection == Rapoo::Connection::Bluetooth)
                s.via = L"Bluetooth";
            else
                s.via = L"2.4 GHz receiver";
            s.percent = battery.percent;
            s.charging = battery.charging;
            s.graceSec = 1800;  // existing UI keeps the last reading, dimmed, while the mouse sleeps

            // A charging mouse can report through both cable and receiver. Keep a single entry,
            // preferring the cable's state but keeping any available percentage. Claim both
            // products before generic fallbacks.
            auto existing = std::find_if(ctx.out.begin(), ctx.out.end(), [&](const DeviceStatus& other) {
                return other.key == s.key;
            });
            if (existing == ctx.out.end()) ctx.Add(s, vid, pid);
            else {
                if (s.via == L"USB cable") {
                    int receiverPercent = existing->percent;
                    *existing = s;
                    if (existing->percent < 0) existing->percent = receiverPercent;
                } else if (existing->percent < 0 && s.percent >= 0) {
                    existing->percent = s.percent;
                }
                ctx.Claim(vid, pid);
            }
            break;
        }
    }
    for (auto it = g_rapoo.begin(); it != g_rapoo.end();)
        it = present.count(it->first) ? std::next(it) : g_rapoo.erase(it);
}
