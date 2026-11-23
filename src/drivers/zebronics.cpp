// Zebronics / other Compx-based wireless mice on the 2.4 GHz receiver 3554:FC0A.
//
// The receiver exposes a mouse interface with a vendor collection FF02:0002 carrying a 7-byte
// feature report, id 06. A GET_FEATURE request is read-only and returns e.g.:
//     06 00 02 00 DB 64 14 80
//     06 00 02 00 DC 64 14 80
// byte 0 = report id, byte 4 = rolling counter (increments per query), byte 5 = battery percent
// (0x64 = 100). Bytes 1,2,3,6,7 stayed constant across captures, including with the battery
// removed (the receiver answers from cached state). The mouse is battery-only, so no charging flag.
//
// Source: local USB capture on a Zebronics wireless mouse with receiver 3554:FC0A. The same VID is
// shared by other Compx/Redragon receivers, but this PID and report shape are specific to it.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x3554;
constexpr USHORT kPid = 0xFC0A;
constexpr BYTE kReport = 0x06;

bool LooksLikeReply(const std::vector<BYTE>& r) {
    return r.size() >= 8 && r[0] == kReport && r[1] == 0x00 && r[2] == 0x02;
}

}  // namespace

void PollZebronics(PollContext& ctx) {
    for (auto [vid, pid] : ctx.Products(kVid)) {
        if (pid != kPid) continue;
        auto colls = ctx.Find(vid, pid, [](const HidColl& c) {
            return c.page() == 0xFF02 && c.usage() == 0x0002 && c.featLen() >= 8;
        });
        for (const HidColl* c : colls) {
            HidDev d;
            if (!d.OpenFeatureOnly(*c)) continue;
            std::vector<BYTE> r;
            if (!d.GetFeature(kReport, r) || !LooksLikeReply(r)) continue;
            int pct = r[5];
            if (pct < 0 || pct > 100) continue;
            DeviceStatus s;
            s.key = ctx.StableKey(L"zebronics:" + Hex4(vid) + L":" + Hex4(pid));
            s.name = L"Zebronics wireless mouse";
            s.via = L"2.4 GHz receiver";
            s.kind = DeviceKind::Mouse;
            s.percent = pct;
            s.graceSec = 1800;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
