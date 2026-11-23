// SteelSeries wireless headsets and mice (VID 1038).
// Headset protocols follow github.com/Sapd/HeadsetControl, mouse protocols github.com/flozz/rivalcfg
// (Aerox 3 Wireless Gen 2: rivalcfg issue #276 and its device_aerox3_wireless_gen2 branch). The Aerox
// models were cross-checked against github.com/yurtemre7/steel-mouse.
#include "drivers.h"

#include <algorithm>
#include <iterator>

namespace {

bool In(USHORT pid, std::initializer_list<USHORT> list) { return std::find(list.begin(), list.end(), pid) != list.end(); }
bool In(USHORT pid, const std::vector<USHORT>& list) { return std::find(list.begin(), list.end(), pid) != list.end(); }

bool WriteRead(HidDev& d, std::vector<BYTE> req, std::vector<BYTE>& resp, int timeoutMs = 1000) {
    d.Flush();
    return d.Write(req) && d.Read(resp, timeoutMs) > 0;
}

DeviceStatus Headset(USHORT pid, const wchar_t* name) {
    DeviceStatus s;
    s.key = L"steelseries:" + Hex4(pid);
    s.name = name;
    s.kind = DeviceKind::Headset;
    s.via = L"2.4 GHz receiver";
    return s;
}

// ---- headsets ----------------------------------------------------------------------------------

// Arctis 1 / 1 Xbox / 7X: 06 12 -> [2]==01 offline, [3] %.
bool Arctis1(HidDev& d, USHORT pid, DeviceStatus& s) {
    std::vector<BYTE> r;
    if (!WriteRead(d, { 0x06, 0x12 }, r) || r.size() < 4) return false;
    s = Headset(pid, L"SteelSeries Arctis 1 Wireless");
    s.connected = r[2] != 0x01;
    if (s.connected) s.percent = Clamp100(r[3]);
    return true;
}

// Arctis 7 / 7 2019 / Pro 2019: 06 18 -> [2] %.
bool Arctis7(HidDev& d, USHORT pid, DeviceStatus& s) {
    std::vector<BYTE> r;
    if (!WriteRead(d, { 0x06, 0x18 }, r) || r.size() < 3) return false;
    s = Headset(pid, pid == 0x1252 ? L"SteelSeries Arctis Pro" : L"SteelSeries Arctis 7");
    s.percent = Clamp100(r[2]);
    s.connected = r[2] != 0;
    return true;
}

// Arctis 9: 00 20 -> [3] raw (0x64..0x9A), [4]==01 charging.
bool Arctis9(HidDev& d, USHORT pid, DeviceStatus& s) {
    std::vector<BYTE> r;
    if (!WriteRead(d, { 0x00, 0x20 }, r) || r.size() < 5) return false;
    s = Headset(pid, L"SteelSeries Arctis 9");
    s.percent = MapTo100(r[3], 0x64, 0x9A);
    s.charging = r[4] == 0x01;
    return true;
}

// Arctis Pro Wireless: 41 AA -> [0]==02 offline (04 online); 40 AA -> [0] level 0..4.
// HeadsetControl writes 41 AA through hidapi, which treats the first byte as a report ID. That only
// reaches the base station on Linux; interface 0 does not number its reports (33-byte output report
// = ID 0 + 32 bytes), so on Windows the command has to go behind a 0 report ID or it never arrives.
bool ArctisProCmd(HidDev& d, BYTE cmd, std::vector<BYTE>& r) {
    if (WriteRead(d, { 0x00, cmd, 0xAA }, r) && !r.empty()) return true;
    return WriteRead(d, { cmd, 0xAA }, r) && !r.empty();  // in case a firmware numbers its reports
}

bool ArctisProWireless(HidDev& d, USHORT pid, DeviceStatus& s) {
    std::vector<BYTE> r;
    if (!ArctisProCmd(d, 0x41, r)) return false;
    s = Headset(pid, L"SteelSeries Arctis Pro Wireless");
    s.connected = r[0] != 0x02;
    if (s.connected && ArctisProCmd(d, 0x40, r) && r[0] <= 4) s.percent = MapTo100(r[0], 0, 4);
    return true;
}

// Arctis Nova Pro Wireless: 06 B0 -> [15] status (01 offline, 02 charging), [6] level 0..8.
bool NovaProWireless(HidDev& d, USHORT pid, DeviceStatus& s) {
    std::vector<BYTE> r;
    if (!WriteRead(d, { 0x06, 0xB0 }, r) || r.size() < 16) return false;
    s = Headset(pid, L"SteelSeries Arctis Nova Pro Wireless");
    s.connected = r[15] != 0x01;
    s.charging = r[15] == 0x02;
    if (s.connected) s.percent = MapTo100(r[6], 0, 8);
    return true;
}

// Nova family: 00 B0 -> status block; layout depends on the model.
bool NovaFamily(HidDev& d, USHORT pid, DeviceStatus& s) {
    std::vector<BYTE> r;
    if (!WriteRead(d, { 0x00, 0xB0 }, r) || r.size() < 7) return false;
    if (In(pid, { 0x220E, 0x2212, 0x2216, 0x2236 })) {  // Arctis 7+
        s = Headset(pid, L"SteelSeries Arctis 7+");
        s.connected = r[1] != 0x01;
        s.percent = MapTo100(r[2], 0, 4);
        s.charging = r[3] == 0x01;
    } else if (In(pid, { 0x2269, 0x226D, 0x2232, 0x2253 })) {  // Nova 3P Wireless, Nova 5
        s = Headset(pid, In(pid, { 0x2232, 0x2253 }) ? L"SteelSeries Arctis Nova 5" : L"SteelSeries Arctis Nova 3P Wireless");
        s.connected = r[1] != 0x02;
        s.percent = Clamp100(r[3]);
        s.charging = r[4] == 0x01;
    } else if (pid == 0x230A) {  // Arctis GameBuds: lowest of the buds that are out of the case
        s = Headset(pid, L"SteelSeries Arctis GameBuds");
        int lvl = -1;
        if (r[3] == 0x03) lvl = r[5];
        if (r[4] == 0x03) lvl = lvl < 0 ? r[6] : std::min<int>(lvl, r[6]);
        s.connected = lvl >= 0;
        s.percent = lvl >= 0 ? Clamp100(lvl) : -1;
    } else {  // Arctis Nova 7 / 7X / 7P and variants
        bool discrete = In(pid, { 0x2202, 0x2206, 0x220A, 0x223A, 0x227A, 0x22A4 });
        s = Headset(pid, L"SteelSeries Arctis Nova 7");
        s.connected = r[3] != 0x00;
        s.charging = r[3] == 0x01 || r[3] == 0x02;
        s.percent = discrete ? MapTo100(r[2], 0, 4) : Clamp100(r[2]);
    }
    if (!s.connected) s.percent = -1;
    return true;
}

// ---- mice --------------------------------------------------------------------------------------

DeviceStatus Mouse(USHORT pid, const wchar_t* name, bool wired) {
    DeviceStatus s;
    s.key = L"steelseries:" + Hex4(pid);
    s.name = name;
    s.kind = DeviceKind::Mouse;
    s.via = wired ? L"USB cable" : L"2.4 GHz receiver";
    return s;
}

// Sends `cmd` and returns the first input report that echoes it. SteelSeries GG keeps its own
// commands going on the same interface and the receiver only answers the latest one, so a single
// query is often lost: it is resent every 100 ms until the echo arrives, within one 1 s deadline.
// Reports that do not echo `cmd` (GG's replies, acknowledgements) are skipped, except 40 FF: the
// receiver's answer while it cannot reach the mouse (seen with the mouse switched off), which ends
// the wait at once.
bool QueryEcho(HidDev& d, BYTE cmd, std::vector<BYTE>& r, size_t minLen) {
    d.Flush();
    const ULONGLONG start = GetTickCount64(), end = start + 1000;
    ULONGLONG resend = start;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= end) return false;
        if (now >= resend) {
            if (!d.Write({ 0x00, cmd })) return false;
            resend = now + 100;
        }
        int wait = (int)(std::min(resend, end) - now);
        if (wait <= 0 || d.Read(r, wait) < 0) continue;
        if (r.size() >= minLen && r[0] == cmd) return true;
        if (r.size() >= 2 && r[0] == 0x40 && r[1] == 0xFF) return false;
    }
}

// Aerox / Prime Wireless family on interface 3: 92 on the cable, D2 through the 2.4 GHz receiver
// (commands to the receiver carry the 0x40 flag). The reply comes in one of two layouts:
//  * steps: [1] = (level/5 + 1) | charging<<7, and 00 means the mouse is off or asleep. The CS2
//    Dragon Lore Edition uses it too, checked on hardware: D2 0A (45%) on its receiver, 92 8B (50%,
//    charging) on its cable.
//  * percent (Aerox 3 Wireless Gen 2): [1] 00 on battery, 01 charging, 02 charged with the cable
//    still in; [2] = 0..100, or FF while the mouse has not measured yet (just after plugging or
//    unplugging the cable). The receiver answering 00 00 is taken as the mouse being off, like the
//    00 of the step layout.
// A mouse that is off or asleep (00, 00 00, or the receiver's 40 FF) is left out of the results, so
// the tray keeps its last level, dimmed, for graceSec, as with the other receivers.
struct AeroxModel {
    USHORT pid;
    USHORT receiverPid;  // stable identity on both the receiver and the cable
    const wchar_t* name;
    bool wired;
    bool percent;  // Gen 2 layout
};

const AeroxModel kAerox[] = {
    { 0x1838, 0x1838, L"SteelSeries Aerox 3 Wireless", false, false },
    { 0x183A, 0x1838, L"SteelSeries Aerox 3 Wireless", true, false },
    { 0x1878, 0x1878, L"SteelSeries Aerox 3 Wireless CS2 Dragon Lore Edition", false, false },
    { 0x187A, 0x1878, L"SteelSeries Aerox 3 Wireless CS2 Dragon Lore Edition", true, false },
    { 0x1890, 0x1890, L"SteelSeries Aerox 3 Wireless Gen 2", false, true },
    { 0x1892, 0x1890, L"SteelSeries Aerox 3 Wireless Gen 2", true, true },
    { 0x1852, 0x1852, L"SteelSeries Aerox 5 Wireless", false, false },
    { 0x1854, 0x1852, L"SteelSeries Aerox 5 Wireless", true, false },
    // Destiny 2 Lightfall Edition: same Aerox 5 protocol, different USB IDs.
    { 0x185C, 0x185C, L"SteelSeries Aerox 5 Wireless Destiny 2 Lightfall Edition", false, false },
    { 0x185E, 0x185C, L"SteelSeries Aerox 5 Wireless Destiny 2 Lightfall Edition", true, false },
    { 0x1858, 0x1858, L"SteelSeries Aerox 9 Wireless", false, false },
    { 0x185A, 0x1858, L"SteelSeries Aerox 9 Wireless", true, false },
    { 0x1840, 0x1840, L"SteelSeries Prime Wireless", false, false },
    { 0x1842, 0x1840, L"SteelSeries Prime Wireless", true, false },
    { 0x1860, 0x1860, L"SteelSeries Aerox 5 Wireless Diablo IV Edition", false, false },  // untested hardware
};

std::vector<USHORT> AeroxPids() {
    std::vector<USHORT> pids;
    for (const auto& m : kAerox) pids.push_back(m.pid);
    return pids;
}

bool Aerox(HidDev& d, USHORT pid, DeviceStatus& s) {
    auto m = std::find_if(std::begin(kAerox), std::end(kAerox), [&](const AeroxModel& a) { return a.pid == pid; });
    if (m == std::end(kAerox)) return false;
    const BYTE cmd = m->wired ? 0x92 : 0xD2;
    std::vector<BYTE> r;
    if (!QueryEcho(d, cmd, r, m->percent ? 3 : 2)) return false;
    if (m->percent && r[1] > 0x02) return false;  // not a Gen 2 status byte
    bool off = m->percent ? (!m->wired && r[1] == 0x00 && r[2] == 0x00) : (r[1] & 0x7F) == 0;
    if (off) return false;  // left out: the tray keeps the last level for graceSec
    s = Mouse(pid, m->name, m->wired);
    s.key = L"steelseries:" + Hex4(m->receiverPid);
    s.graceSec = 1800;
    if (m->percent) {
        s.charging = r[1] != 0x00;
        if (r[2] <= 100) s.percent = r[2];  // FF (not measured yet) and invalid levels stay unknown
    } else if ((r[1] & 0x7F) <= 21) {  // only levels 1..21 represent 0..100%; invalid levels stay unknown
        s.percent = ((r[1] & 0x7F) - 1) * 5;
        s.charging = (r[1] & 0x80) != 0;
    }
    return true;
}

// Rival 3 Wireless (+ Gen 2), Rival 650: AA 01 -> [0] %, [2] charging.
bool Rival(HidDev& d, USHORT pid, DeviceStatus& s) {
    std::vector<BYTE> r;
    if (!WriteRead(d, { 0x00, 0xAA, 0x01 }, r, 300) || r.size() < 3) return false;
    bool rival650 = pid == 0x172B || pid == 0x1726;
    s = Mouse(pid, rival650 ? L"SteelSeries Rival 650 Wireless" : pid == 0x1872 ? L"SteelSeries Rival 3 Wireless Gen 2" : L"SteelSeries Rival 3 Wireless",
              pid == 0x172B);
    if (rival650) s.key = L"steelseries:172B";  // one entry whether it answers on the cable or the receiver
    s.percent = Clamp100(r[0]);
    s.charging = r[2] != 0;
    return true;
}

// Rival Pro (UWB, hot-swappable batteries): 92 -> 92 00 <mouse %> <flag> <spare battery %>.
// From the Rival Pro support pull request on rivalcfg (github.com/flozz/rivalcfg/pull/294).
bool RivalPro(HidDev& d, USHORT pid, DeviceStatus& s) {
    d.Flush();
    if (!d.Write({ 0x00, 0x92 })) return false;
    std::vector<BYTE> r;
    ULONGLONG end = GetTickCount64() + 500;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0 || d.Read(r, left) < 3) return false;
        if (r[0] == 0x92 && r[1] == 0x00) break;
    }
    if (r[2] > 100) return false;
    s = Mouse(pid, L"SteelSeries Rival Pro", false);
    s.via = L"UWB base station";
    s.percent = r[2];
    return true;
}

struct Model {
    std::vector<USHORT> pids;
    bool (*accept)(const HidColl&);
    bool (*poll)(HidDev&, USHORT, DeviceStatus&);
};

bool PageFF43(const HidColl& c) { return c.page() == 0xFF43 && c.usage() == 0x0202; }
bool PageFFC0(const HidColl& c) { return c.page() == 0xFFC0 && c.usage() == 0x0001; }
bool Iface0(const HidColl& c) { return c.iface == 0 && c.outLen() > 0; }
bool Iface2(const HidColl& c) { return c.iface == 2 && c.outLen() > 0; }
bool Iface3(const HidColl& c) { return c.iface == 3 && c.outLen() > 0; }
bool Iface4(const HidColl& c) { return c.iface == 4 && c.outLen() > 0; }

const Model kModels[] = {
    { { 0x12B3, 0x12B6, 0x12D7 }, PageFF43, Arctis1 },
    { { 0x1260, 0x12AD, 0x1252 }, PageFF43, Arctis7 },
    { { 0x12C2 }, Iface0, Arctis9 },
    { { 0x1290 }, Iface0, ArctisProWireless },
    { { 0x12E0, 0x225D }, Iface4, NovaProWireless },
    { { 0x220E, 0x2212, 0x2216, 0x2236, 0x2269, 0x226D, 0x2232, 0x2253, 0x230A, 0x2202, 0x22A1, 0x227E, 0x2206, 0x2258, 0x229E,
        0x22AD, 0x223A, 0x22A9, 0x227A, 0x22A4, 0x22A5, 0x220A, 0x22A7 },
      PageFFC0, NovaFamily },
    { AeroxPids(), Iface3, Aerox },
    { { 0x1830, 0x1872 }, Iface3, Rival },
    { { 0x172B, 0x1726 }, Iface0, Rival },  // Rival 650: cable, 2.4 GHz receiver
    { { 0x1880 }, Iface2, RivalPro },
};

}  // namespace

void PollSteelSeries(PollContext& ctx) {
    for (auto [vid, pid] : ctx.Products(0x1038)) {
        for (const auto& m : kModels) {
            if (!In(pid, m.pids)) continue;
            for (const HidColl* c : ctx.Find(vid, pid, m.accept)) {
                HidDev d;
                DeviceStatus s;
                if (!d.Open(*c) || !m.poll(d, pid, s)) continue;
                // Aerox/Prime and Rival 650 can answer on both products; keep one entry, preferring the cable.
                auto dup = std::find_if(ctx.out.begin(), ctx.out.end(), [&](const DeviceStatus& o) { return o.key == s.key; });
                if (dup == ctx.out.end()) ctx.Add(s, vid, pid);
                else {
                    if (s.via == L"USB cable") { *dup = s; dup->vid = vid; }
                    ctx.Claim(vid, pid);
                }
                break;
            }
            break;
        }
    }
}
