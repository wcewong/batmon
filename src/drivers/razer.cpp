// Razer wireless mice, keyboards and dongles (VID 1532).
// 90-byte feature report (report id 0):
//   [0] status  [1] transaction id  [2..3] remaining packets  [4] protocol type  [5] data size
//   [6] command class  [7] command id  [8..87] arguments  [88] crc (xor of 2..87)  [89] reserved
// Battery: class 07 id 80 -> args[1] = 0..255. Charging: class 07 id 84 -> args[1] = 1.
// Reply status: 01 busy, 02 ok, 03 failed, 04 timeout, 05 not supported.
// Use documented keyboard transaction ids; probe the known ids for other models.
// Reference: github.com/openrazer/openrazer (protocol documentation in its driver sources).
#include "drivers.h"

#include <algorithm>
#include <array>
#include <map>

namespace {

BYTE KeyboardBatteryTid(USHORT pid) {
    // OpenRazer razerkbd_driver.c: razer_attr_read_charge_level / charge_status.
    switch (pid) {
        case 0x025C: case 0x0271: case 0x0290: case 0x0296:
        case 0x02BA: case 0x02C9: case 0x02D2: case 0x02D5: return 0x9F;  // wireless keyboards
        case 0x0258: case 0x0292: case 0x0298: case 0x02B9:
        case 0x02CD: case 0x02D4: case 0x02D7: return 0x1F;  // wired keyboards / Joro
        case 0x025A: return 0x3F;  // BlackWidow V3 Pro wired
        default: return 0;
    }
}

enum class Rz { Ok, Offline, Unsupported, NoReply };

Rz Command(HidDev& d, BYTE tid, BYTE cls, BYTE id, BYTE size, std::vector<BYTE>& args, BYTE firstArg = 0) {
    std::vector<BYTE> rep(91, 0);  // [0] = report id 0, then the 90-byte report
    BYTE* r = rep.data() + 1;
    r[1] = tid;
    r[5] = size;
    r[6] = cls;
    r[7] = id;
    r[8] = firstArg;
    BYTE crc = 0;
    for (int i = 2; i < 88; i++) crc ^= r[i];
    r[88] = crc;
    std::vector<BYTE> resp;
    bool send = true;
    Rz res = Rz::NoReply;
    const bool slotQuery = cls == 0x00 && id == 0xBF;
    for (int attempt = 0; attempt < (slotQuery ? 4 : 8); attempt++) {
        if (send && !d.SetFeature(rep)) return Rz::NoReply;
        // The slot list is local to the receiver. A long wait lets Synapse replace the reply.
        Sleep(slotQuery || KeyboardBatteryTid(d.coll().pid) ? 5 : (attempt == 0 ? 15 : 40));
        send = true;
        res = Rz::NoReply;
        if (!d.GetFeature(0, resp) || resp.size() < 91) continue;
        const BYTE* a = resp.data() + 1;
        BYTE replyCrc = 0;
        for (int i = 2; i < 88; i++) replyCrc ^= a[i];
        // Synapse shares the feature-report mailbox. A different reply needs a fresh request.
        if (a[1] != tid || a[2] || a[3] || a[4] || a[5] != size || a[6] != cls || a[7] != id ||
            a[88] != replyCrc) continue;
        if (a[0] == 0x01) { send = false; continue; }  // busy: wait for this request to finish
        if (a[0] == 0x02) { args.assign(a + 8, a + 88); return Rz::Ok; }
        if (a[0] == 0x04) { res = Rz::Offline; continue; }  // retry a transient receiver timeout
        if (a[0] == 0x05 || a[0] == 0x03) return Rz::Unsupported;
    }
    return res;
}

std::wstring CleanName(std::wstring n) {
    for (const wchar_t* suffix : { L" Dongle", L" (Wired)", L" (Wireless)", L" Wireless Receiver", L" Receiver", L" HyperSpeed Dongle" }) {
        size_t l = wcslen(suffix);
        if (n.size() > l && n.compare(n.size() - l, l, suffix) == 0) n = n.substr(0, n.size() - l);
    }
    if (n.empty()) n = L"Razer device";
    return n;
}

// Exact keyboard/keypad/laptop and mouse/receiver IDs from OpenRazer (no PID-range assumptions).
// https://github.com/openrazer/openrazer/blob/cd5c4243bfafb08df7e2d2963367c3c150626eac/driver/razerkbd_driver.h
// https://github.com/openrazer/openrazer/blob/cd5c4243bfafb08df7e2d2963367c3c150626eac/driver/razermouse_driver.h
constexpr USHORT kKeyboardPids[] = {
    0x010D, 0x010E, 0x010F, 0x0111, 0x0113, 0x0118, 0x011A, 0x011B, 0x011C, 0x0201,
    0x0202, 0x0203, 0x0204, 0x0205, 0x0207, 0x0208, 0x0209, 0x020F, 0x0210, 0x0211,
    0x0214, 0x0216, 0x0217, 0x021A, 0x021E, 0x021F, 0x0220, 0x0221, 0x0224, 0x0225,
    0x0226, 0x0227, 0x0228, 0x022A, 0x022B, 0x022C, 0x022D, 0x022F, 0x0232, 0x0233,
    0x0234, 0x0235, 0x0237, 0x0239, 0x023A, 0x023B, 0x023F, 0x0240, 0x0241, 0x0243,
    0x0244, 0x0245, 0x0246, 0x024A, 0x024B, 0x024C, 0x024D, 0x024E, 0x0252, 0x0253,
    0x0255, 0x0256, 0x0257, 0x0258, 0x0259, 0x025A, 0x025C, 0x025D, 0x025E, 0x0266,
    0x0268, 0x0269, 0x026A, 0x026B, 0x026C, 0x026D, 0x026E, 0x026F, 0x0270, 0x0271,
    0x0276, 0x0279, 0x027A, 0x0282, 0x0287, 0x028A, 0x028B, 0x028C, 0x028D, 0x028F,
    0x0290, 0x0292, 0x0293, 0x0294, 0x0295, 0x0296, 0x0298, 0x029D, 0x029E, 0x029F,
    0x02A0, 0x02A1, 0x02A2, 0x02A3, 0x02A5, 0x02A6, 0x02A7, 0x02B0, 0x02B6, 0x02B8,
    0x02B9, 0x02BA, 0x02C5, 0x02C6, 0x02C7, 0x02C9 /* BlackWidow V4 Low-profile HyperSpeed */, 0x02CD,
    0x02CF, 0x02D2, 0x02D4, 0x02D5, 0x02D7, 0x02E0, 0x02E1, 0x02EA, 0x0A24,
};

constexpr USHORT kMousePids[] = {
    0x0013, 0x0015, 0x0016, 0x001F, 0x0020, 0x0024, 0x0025, 0x0029, 0x002E, 0x002F,
    0x0032, 0x0034, 0x0036, 0x0037, 0x0038, 0x0039, 0x003E, 0x003F, 0x0040, 0x0041,
    0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0048, 0x004C, 0x004F, 0x0050, 0x0053,
    0x0054, 0x0059, 0x005A, 0x005B, 0x005C, 0x005E, 0x005F, 0x0060, 0x0062, 0x0064,
    0x0065, 0x0067, 0x006A, 0x006B, 0x006C, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072,
    0x0073, 0x0077, 0x0078, 0x007A, 0x007B, 0x007C, 0x007D, 0x0080, 0x0083, 0x0084,
    0x0085, 0x0086, 0x0088, 0x008A, 0x008C, 0x008D, 0x008F, 0x0090, 0x0091, 0x0094,
    0x0095, 0x0096, 0x0098, 0x0099, 0x009A, 0x009C, 0x009E, 0x009F, 0x00A1, 0x00A3,
    0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00AA, 0x00AB, 0x00AF, 0x00B0, 0x00B2, 0x00B3,
    0x00B4, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BE, 0x00BF, 0x00C0, 0x00C1, 0x00C2,
    0x00C3, 0x00C4, 0x00C5, 0x00C7, 0x00C8, 0x00CB, 0x00CC, 0x00CD, 0x00D0, 0x00D1,
    0x00D3, 0x00D4, 0x00D6, 0x00D7, 0x00DA, 0x00DB, 0x00DE, 0x00DF, 0x00E0, 0x00E5,
    0x00E6,
};

DeviceKind Kind(const std::vector<HidColl>& colls, USHORT vid, USHORT pid) {
    // Both keyboards and mice can expose keyboard AND mouse collections for macros/buttons.
    if (vid == 0x1532) {
        for (USHORT p : kKeyboardPids) if (p == pid) return DeviceKind::Keyboard;
        for (USHORT p : kMousePids) if (p == pid) return DeviceKind::Mouse;
    }
    return KindFromCollections(colls, vid, pid);
}

struct ReceiverSlot {
    USHORT pid;
    BYTE tid;
    bool active;
};

bool ReadReceiverSlots(HidDev& d, std::array<ReceiverSlot, 2>& slots) {
    // HyperSpeed Multi-Device slot list (capture in OpenRazer issue #1892).
    // Each entry is [active, PID high, PID low]. The mouse uses 1F and the keyboard uses 9F.
    std::vector<BYTE> args;
    if (Command(d, 0x1F, 0x00, 0xBF, 0x31, args, 0x10) != Rz::Ok || args[0] != slots.size())
        return false;
    if (args[1] > 1 || args[4] > 1) return false;
    slots = {{
        { static_cast<USHORT>((args[2] << 8) | args[3]), 0x1F, args[1] == 1 },
        { static_cast<USHORT>((args[5] << 8) | args[6]), 0x9F, args[4] == 1 },
    }};
    // A vacant slot falls back to the normal single-device path.
    return slots[0].pid && slots[0].pid != 0xFFFF && slots[1].pid && slots[1].pid != 0xFFFF &&
           slots[0].pid != slots[1].pid;
}

std::wstring SlotName(const PollContext& ctx, USHORT pid, DeviceKind kind) {
    for (const auto& c : ctx.colls) {
        if (c.vid != 0x1532 || c.pid != pid) continue;
        std::wstring name = HidProduct(c);
        if (!name.empty()) return CleanName(name);
    }
    // A child on the shared receiver has no USB product string of its own.
    switch (pid) {
        case 0x00A8: return L"Razer Naga V2 Pro";
        case 0x00DF: return L"Razer Viper V3 Pro SE";
        case 0x0296: return L"Razer DeathStalker V2 Pro Tenkeyless";
        case 0x02D5: return L"Razer BlackWidow V4 Tenkeyless HyperSpeed";
    }
    return std::wstring(kind == DeviceKind::Mouse ? L"Razer Mouse (" : L"Razer Keyboard (") + Hex4(pid) + L")";
}

void SetBatteryReading(DeviceStatus& s, HidDev& d, BYTE tid, Rz res, const std::vector<BYTE>& args) {
    s.connected = res == Rz::Ok;
    if (!s.connected) return;
    s.percent = Clamp100((args[1] * 100 + 127) / 255);
    std::vector<BYTE> ch;
    if (Command(d, tid, 0x07, 0x84, 0x02, ch) == Rz::Ok) s.charging = ch[1] == 1;
}

}  // namespace

void PollRazer(PollContext& ctx) {
    struct LastReading { std::wstring path; DeviceStatus status; };
    static std::map<USHORT, LastReading> lastGood;
    for (auto it = lastGood.begin(); it != lastGood.end();) {
        bool present = false;
        for (const auto& c : ctx.colls)
            if (c.path == it->second.path) { present = true; break; }
        if (present) { ++it; continue; }
        // A removed receiver is different from a mouse sleeping behind it.
        DeviceStatus gone = it->second.status;
        gone.connected = false;
        gone.percent = -1;
        gone.charging = false;
        ctx.out.push_back(gone);
        it = lastGood.erase(it);
    }
    const auto firstRazer = ctx.out.size();
    auto addReading = [&](const DeviceStatus& s, USHORT vid, USHORT pid) {
        // The same device can answer through its cable, its receiver or a shared receiver.
        auto existing = std::find_if(ctx.out.begin() + firstRazer, ctx.out.end(),
                                     [&](const DeviceStatus& o) { return o.key == s.key || o.name == s.name; });
        if (existing == ctx.out.end()) {
            ctx.Add(s, vid, pid);
        } else {
            ctx.Claim(vid, pid);
            if (s.percent >= 0 && existing->percent < 0) {  // a live reading wins over an asleep one
                existing->connected = true;
                existing->percent = s.percent;
                existing->asleep = false;
            }
            existing->charging |= s.charging;
        }
    };
    for (auto [vid, pid] : ctx.Products(0x1532)) {
        DeviceKind kind = Kind(ctx.colls, vid, pid);
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) { return c.featLen() == 91; })) {
            HidDev d;
            if (!d.OpenFeatureOnly(*c)) continue;
            std::vector<BYTE> args;
            std::array<ReceiverSlot, 2> slots;
            if ((kind == DeviceKind::Mouse || kind == DeviceKind::Keyboard) && ReadReceiverSlots(d, slots)) {
                for (const auto& slot : slots) {
                    auto res = slot.active ? Command(d, slot.tid, 0x07, 0x80, 0x02, args) : Rz::Offline;
                    if (res != Rz::Ok && res != Rz::Offline) continue;
                    DeviceStatus s;
                    s.key = L"razer:" + Hex4(slot.pid);
                    s.kind = slot.tid == 0x1F ? DeviceKind::Mouse : DeviceKind::Keyboard;
                    s.name = SlotName(ctx, slot.pid, s.kind);
                    s.via = L"Razer receiver";
                    s.graceSec = 120;
                    SetBatteryReading(s, d, slot.tid, res, args);
                    addReading(s, vid, pid);
                }
                ctx.Claim(vid, pid);
                break;
            }

            Rz res = Rz::NoReply;
            BYTE tid = 0;
            bool sawOffline = false;
            BYTE keyboardTid = KeyboardBatteryTid(pid);
            std::vector<BYTE> tids = keyboardTid ? std::vector<BYTE>{ keyboardTid } :
                                                  std::vector<BYTE>{ 0x1F, 0x3F, 0xFF, 0x08, 0x9F };
            for (BYTE t : tids) {
                Rz r = Command(d, t, 0x07, 0x80, 0x02, args);
                if (r == Rz::Ok) { res = r; tid = t; break; }
                sawOffline |= r == Rz::Offline;  // may also mean "wrong transaction id": keep trying
            }
            if (res != Rz::Ok && sawOffline) res = Rz::Offline;
            if (res != Rz::Ok && res != Rz::Offline) continue;

            DeviceStatus s;
            s.key = L"razer:" + Hex4(pid);
            s.name = CleanName(HidProduct(*c));
            s.kind = kind;
            if (s.kind == DeviceKind::Other) s.kind = GuessKind(s.name);
            s.via = L"Razer receiver";
            // The Viper V2 Pro changes PID between its cable (00A5) and its receiver (00A6, PR #1422): one key,
            // so its history, name and position survive a cable change.
            if (pid == 0x00A5 || pid == 0x00A6) {
                s.key = L"razer:00A6";
                if (pid == 0x00A5) s.via = L"USB cable";
            }
            s.graceSec = 120;  // retain the last reading briefly when Synapse prevents a valid reply
            SetBatteryReading(s, d, tid, res, args);
            if (res == Rz::Offline) {
                // Receiver timeouts cannot distinguish sleep from the power switch (PR #3787).
                // Keep a previously seen mouse dimmed while its receiver remains plugged in.
                s.asleep = s.kind == DeviceKind::Mouse && lastGood.count(pid) != 0;
                s.connected = s.asleep;
            } else if (s.kind == DeviceKind::Mouse) {
                lastGood[pid] = { c->path, s };
            }
            addReading(s, vid, pid);
            break;
        }
    }
}
