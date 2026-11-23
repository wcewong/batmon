// AULA F75 / F87 Pro / F99, MCHOSE (K99 V2, G75 Pro, X75 ...) and Redragon Horus Pro (K618RGB-PRO)
// wireless keyboards on the "BY" 2.4 GHz receivers: 3554:FA09 (Compx, shared by these brands) and MCHOSE's
// own 41E4:2001.
// MCHOSE K87 / K99 receivers also come as 05AC:024F (Apple's vendor id), the id the SONiX dongles in
// sonix.cpp use. M HUB lists 1452/591 among its devices; the MCHOSE ones are told apart by layout:
// the BY collection on interface 1 (FF02:0002, 20-byte input and output reports) next to an
// FF04:0002 feature collection, where the SONiX dongles only have FF60:0061 on interface 3.
// Only that exact layout is probed on this id (issue #1417, MCHOSE K87).
// The receiver exposes a 20-byte vendor collection (usage page FF02 or FF04, usage 02). Battery is a
// 20-byte output report 0x13 carrying command 0x4A and a trailing sum checksum. The reply is
// another 0x13 frame whose command byte, with its top bit masked, is 0x4A. Its payload starts at
// byte 5: the level (1..100), then the state byte (low nibble: full, high nibble: charging; AULA
// reports 0x01 on battery and 0x10 with the cable in).
// The receiver also pushes unrelated 0x13 frames (a 0x0A status among them), so the report id
// alone does not identify the reply. MCHOSE firmware leaves the reply's trailer at 0 instead of a
// checksum; such a reply is accepted only as a single-packet frame (byte 2 = 1 packet, byte 3 = index 0).
// A keyboard that is dozing or has just switched to 2.4 GHz leaves the first request unanswered
// (one F75 capture timed out at exactly 1 s), so the request is resent every ~1.3 s within one
// 4-second deadline, the documented exception for slow links. Byte 2 alternates between 00 (PyFlat,
// sanshoku captures on the F75) and 01 (the "packet count" the Womier L65 tool sends on the same
// receiver); the reply is identified by command and checksum either way.
// Receivers named "2.4G Wireless Receiver" / "MCHOSE 2.4G Wireless" are first asked for their
// six-byte model code (command 0x05, read-only), as MCHOSE's M HUB does, to tell an MCHOSE model
// apart; MCHOSE models then get M HUB's exact battery frame (13 4A 01 ...).
// AULA protocol from github.com/PyFlat/Device-Battery-Info (PR 6), github.com/deepan-alve/womier-l65-linux
// (windows/l65w.ps1) and github.com/ushineko/sanshoku issue 36.
// MCHOSE receiver ids, model codes and frames from the official M HUB web driver
// (www.mchose.com.cn, assets/main-DQZs6ZzX.js: HidIndexDeviceFilters, getDevName, keyboardPwds,
// getPassWord, getBattery, getBatteryParser; vendorId 1452 / productId 591 with usage page FF00
// or FF04, getDevName "K87" / "K99" on 591, wireless report id 19 = 0x13), as also documented in
// github.com/wushuai0802/myfinder-device-battery (engine/upstream-protocols.md, sections
// "MCHOSE BY keyboard receiver 3554:FA09" and "MCHOSE 41E4:2001 and other 41E4 identities").
//
// AULA HERO 68 Air (and possibly other AULA keyboards) on the "8K Wireless Receiver" (VID 372E,
// PID 106C) speaks a different, acknowledged protocol on usage page FF60 / usage 61 (interface 2):
//  * 20-byte reports: 09 66, three control bytes, 14 payload bytes, checksum.
//    Byte 3 (low 7 bits) is the frame index 1..5, byte 4 (low 7 bits) the frame type:
//    0E data, 00 ACK, 08 trailer from the host, 07 trailer from the keyboard. The top bits of
//    bytes 2, 3 and 4 form a 3-bit counter that advances with every frame a side sends.
//    Data frame checksum: byte 19 = sum of bytes 1..18.
//  * A message is always 4 data frames (56 bytes) plus a trailer. Host trailer: byte 12 =
//    0xFF - sum of the 56 message bytes, byte 13 = sum of bytes 1..12.
//  * Every data and trailer frame is acknowledged by the other side: bytes 2..4 echoed with the
//    type bits cleared (counter bit kept), byte 5 = sum of bytes 1..4. Unacknowledged frames are
//    resent by the keyboard every ~100 ms.
//  * Battery request: message 09 87 00 00 01 00. Reply: 87 00 00 01 00 02 <level> <flags>; the 02
//    is constant (it does not change with charging). The keyboard also pushes FE 05 <level> <flags>
//    on its own. <flags> is 0x10 while charging on the cable and 0x00 on battery.
// From a USB capture of AULA Hub (Wireless-Device-Battery-Monitor issue #201); read-only.
//
// AULA SC580 / SC580X mouse: USB cable 088D:062E, 2.4 GHz receiver 089D:062F. The vendor collection is
// usage page FF02 / usage 0000 with 33-byte reports (report id 0 + 32-byte payload):
//  * payload: [0] command, [1] status (0 in requests), [2] packets (01), [3] data length,
//    [4..30] data, [31] = sum of bytes 4..30.
//  * Command 0x30 (GetDeviceInfo, read-only) is what the vendor app polls for the battery. The reply
//    echoes the command; status 0 OK, 1 busy (retry after 300 ms), 2 mouse offline (asleep or out of
//    range of the receiver), 3 data error (retry). Reply data (from byte 4): "M137" identity,
//    firmware (2), sensor id, unknown, charge state (0 on battery, 1 charging, 2 full), level 0..100,
//    online flag.
//  * sc580ctl notes that the receiver can answer a write with the reply to the previous write, so a reply
//    to another command means "send again".
//  * Observed on 089D:062F: the receiver answers 0x30 within a few ms from its own copy of the mouse
//    state, and pushes D1 <online> <level> <charging> whenever the link goes down or comes back.
//  * Observed with the cable in: the cable interface answers charge state 1 with level 100 regardless
//    of the real charge, and the receiver reports the mouse offline (it switches to the wired link).
// Protocol from github.com/Crucialjun/omarchy-aula-SC580 (PROTOCOL.md, bin/sc580ctl; MIT), reverse
// engineered from the vendor's CBMouse.exe (class CDevDM, MS.ini Protocol=3) and verified on an SC580.
// Tested here on an SC580 over the cable and the receiver.
#include "drivers.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace {

struct Receiver {
    USHORT vid, pid;
};
const Receiver kReceivers[] = { { 0x3554, 0xFA09 }, { 0x3554, 0xFB40 }, { 0x41E4, 0x2001 }, { 0x05AC, 0x024F } };

// 05AC:024F is shared with the SONiX dongles (sonix.cpp), so only the MCHOSE layout is accepted there.
bool IsAppleId(USHORT vid, USHORT pid) { return vid == 0x05AC && pid == 0x024F; }

bool IsByCollection(const HidColl& c) {
    if (!IsAppleId(c.vid, c.pid))
        return (c.page() == 0xFF02 || c.page() == 0xFF04) && c.usage() == 0x0002 && c.outLen() >= 20 && c.inLen() >= 20;
    return c.iface == 1 && c.page() == 0xFF02 && c.usage() == 0x0002 && c.inLen() == 20 && c.outLen() == 20;
}

// The MCHOSE 05AC:024F receiver also has the FF04:0002 feature collection on interface 1.
bool HasMchoseLayout(const PollContext& ctx, USHORT vid, USHORT pid) {
    return !ctx.Find(vid, pid, [](const HidColl& c) {
                   return c.iface == 1 && c.page() == 0xFF04 && c.usage() == 0x0002 && c.featLen() > 0 &&
                          c.inLen() == 0 && c.outLen() == 0;
               }).empty();
}

// M HUB's keyboardPwds table: the six-byte model code returned by command 0x05.
struct Model {
    BYTE code[6];
    const wchar_t* name;
};
const Model kModels[] = {
    { { 0x03, 0x00, 0x00, 0x00, 0x02, 0x1C }, L"MCHOSE G87" },
    { { 0x03, 0x00, 0x00, 0x00, 0x00, 0x77 }, L"MCHOSE K99" },
    { { 0x03, 0x00, 0x00, 0x00, 0x01, 0xDF }, L"MCHOSE K99 V2" },
    { { 0x03, 0x00, 0x00, 0x00, 0x01, 0x32 }, L"MCHOSE G98" },
    { { 0x03, 0x00, 0x00, 0x00, 0x00, 0xD6 }, L"MCHOSE X75" },
    { { 0x0A, 0x00, 0x00, 0x00, 0x00, 0x03 }, L"MCHOSE G98 Pro" },
    { { 0x03, 0x00, 0x00, 0x00, 0x01, 0xC6 }, L"MCHOSE X75 V2" },
    { { 0x03, 0x00, 0x00, 0x00, 0x01, 0x15 }, L"MCHOSE G75 Pro" },
    { { 0x0A, 0x00, 0x00, 0x00, 0x00, 0x18 }, L"MCHOSE G98 V2" },
    { { 0x0A, 0x00, 0x00, 0x00, 0x00, 0x2B }, L"MCHOSE UT98" },
    { { 0x0A, 0x00, 0x00, 0x00, 0x00, 0x01 }, L"MCHOSE Z75" },
    { { 0x0A, 0x00, 0x00, 0x00, 0x00, 0x09 }, L"MCHOSE Z75S" },
    { { 0x03, 0x00, 0x00, 0x00, 0x01, 0xC7 }, L"MCHOSE KX75" },
    { { 0x03, 0x00, 0x00, 0x00, 0x01, 0x74 }, L"MCHOSE GX87" },
    { { 0x03, 0x00, 0x00, 0x00, 0x01, 0xED }, L"MCHOSE GX87 V2" },
    { { 0x03, 0x00, 0x00, 0x00, 0x01, 0x35 }, L"MCHOSE K87" },
    { { 0x03, 0x00, 0x00, 0x00, 0x03, 0x35 }, L"MCHOSE G75 V2" },
    { { 0x0A, 0x00, 0x00, 0x00, 0x00, 0x34 }, L"MCHOSE G87 V2" },
    { { 0x03, 0x00, 0x00, 0x00, 0x00, 0x6A }, L"Redragon Horus Pro" },
    { { 0x03, 0x00, 0x00, 0x00, 0x03, 0xDA }, L"Redragon Horus Pro V2" },
};

BYTE Sum(const std::vector<BYTE>& b, size_t n) {
    unsigned s = 0;
    for (size_t i = 0; i < n; i++) s += b[i];
    return (BYTE)(s & 0xFF);
}

bool SendRequest(HidDev& d, BYTE cmd, BYTE packets) {
    std::vector<BYTE> req(20, 0);
    req[0] = 0x13;
    req[1] = cmd;
    req[2] = packets;
    req[19] = Sum(req, 19);
    return d.Write(req);
}

// Sends 13 <cmd> ... and waits for the matching reply, resending every 1.3 s within `totalMs`.
bool Exchange(HidDev& d, BYTE cmd, std::vector<BYTE>& r, int totalMs) {
    const ULONGLONG start = GetTickCount64(), end = start + totalMs;  // single absolute deadline
    const int kResendMs = 1300;
    int sent = 0;
    d.Flush();
    for (;;) {
        LONGLONG now = (LONGLONG)GetTickCount64();
        if (now >= (LONGLONG)end) return false;
        if (now >= (LONGLONG)start + (LONGLONG)sent * kResendMs) {
            if (!SendRequest(d, cmd, (BYTE)(sent & 1))) return false;
            ++sent;
        }
        LONGLONG nextSend = (LONGLONG)start + (LONGLONG)sent * kResendMs;
        int wait = (int)(std::min<LONGLONG>(nextSend, (LONGLONG)end) - now);
        if (wait < 1) wait = 1;
        ULONGLONG before = GetTickCount64();
        int n = d.Read(r, wait);
        // A read that fails at once is an I/O error (receiver unplugged), not a timeout: stop.
        if (n < 0 && (int)(GetTickCount64() - before) + 20 < wait) return false;
        if (n < 20) continue;  // timeout (resend or give up above) or a short unrelated report
        if (r[0] != 0x13) continue;
        if ((r[1] & 0x7F) != cmd) continue;  // the receiver also pushes unrelated 0x13 frames
        if (std::all_of(r.begin() + 4, r.begin() + 19, [](BYTE b) { return b == 0; })) continue;  // echo of a request
        bool checksum = r[19] == Sum(r, 19);
        bool mchose = r[19] == 0 && r[2] == 1 && r[3] == 0;  // MCHOSE leaves the trailer at 0
        if (!checksum && !mchose) continue;
        return true;
    }
}

// M HUB reads the model code from bytes 5..10. One user capture (myfinder-device-battery issue 476)
// had it one byte earlier, so byte 4 is tried too. A code that matches neither leaves the model unknown.
const wchar_t* IdentifyModel(HidDev& d) {
    std::vector<BYTE> r;
    if (!Exchange(d, 0x05, r, 1300)) return nullptr;
    for (size_t off : { (size_t)5, (size_t)4 })
        for (const auto& m : kModels)
            if (std::memcmp(&r[off], m.code, 6) == 0) return m.name;
    return nullptr;
}

// --- HERO 68 Air / 8K Wireless Receiver (372E:106C) ---

constexpr BYTE kHeroId = 0x09, kHeroMagic = 0x66;
constexpr BYTE kHeroData = 0x0E, kHeroAck = 0x00, kHeroTrailerHost = 0x08, kHeroTrailerDev = 0x07;
constexpr int kHeroDeadlineMs = 1000;  // the whole exchange takes ~30 ms when the keyboard is awake
constexpr int kHeroResendMs = 150;     // resend a frame the keyboard has not acknowledged yet

BYTE SumRange(const std::vector<BYTE>& b, size_t from, size_t to) {
    unsigned s = 0;
    for (size_t i = from; i < to; i++) s += b[i];
    return (BYTE)(s & 0xFF);
}

// Bytes 0..4 of a frame; the 3-bit counter lives in the top bits of bytes 2, 3 and 4.
std::vector<BYTE> HeroHeader(int counter, BYTE index, BYTE type) {
    std::vector<BYTE> f(20, 0);
    f[0] = kHeroId;
    f[1] = kHeroMagic;
    f[2] = (BYTE)(0x05 | ((counter & 4) ? 0x80 : 0));
    f[3] = (BYTE)(index | ((counter & 2) ? 0x80 : 0));
    f[4] = (BYTE)(type | ((counter & 1) ? 0x80 : 0));
    return f;
}

std::vector<BYTE> HeroDataFrame(int counter, BYTE index, const std::vector<BYTE>& msg) {
    std::vector<BYTE> f = HeroHeader(counter, index, kHeroData);
    for (int i = 0; i < 14; i++) f[5 + i] = msg[(index - 1) * 14 + i];
    f[19] = SumRange(f, 1, 19);
    return f;
}

std::vector<BYTE> HeroTrailer(int counter, const std::vector<BYTE>& msg) {
    std::vector<BYTE> f = HeroHeader(counter, 5, kHeroTrailerHost);
    f[12] = (BYTE)(0xFF - SumRange(msg, 0, msg.size()));
    f[13] = SumRange(f, 1, 13);
    return f;
}

std::vector<BYTE> HeroAck(const std::vector<BYTE>& r) {
    std::vector<BYTE> f(20, 0);
    f[0] = kHeroId;
    f[1] = kHeroMagic;
    f[2] = r[2];
    f[3] = r[3];
    f[4] = (BYTE)(r[4] & 0x80);
    f[5] = SumRange(f, 1, 5);
    return f;
}

bool IsAckFor(const std::vector<BYTE>& r, const std::vector<BYTE>& f) {
    return (r[4] & 0x7F) == kHeroAck && r[2] == f[2] && r[3] == f[3] && (r[4] & 0x80) == (f[4] & 0x80) &&
            r[5] == SumRange(r, 1, 5);
}

class HeroLink {
public:
    explicit HeroLink(HidDev& d) : d_(d), end_(GetTickCount64() + kHeroDeadlineMs) {}

    // Sends the 4 data frames and the trailer, each one only after the previous was acknowledged.
    bool SendMessage(const std::vector<BYTE>& msg) {
        int counter = 1;  // AULA Hub starts a session at 1 as well
        for (BYTE i = 1; i <= 4; i++, counter = (counter + 1) & 7)
            if (!SendFrame(HeroDataFrame(counter, i, msg))) return false;
        return SendFrame(HeroTrailer(counter, msg));
    }

    // Reads (and acknowledges) the keyboard's frames until the reply has been received in full.
    bool WaitForBattery() {
        std::vector<BYTE> r;
        while (!(level_ >= 0 && replyDone_)) {
            if (!ReadFrame(r, Remaining())) break;
        }
        return level_ >= 0;
    }

    int level() const { return level_; }
    bool charging() const { return flags_ >= 0 && (flags_ & 0x10) != 0; }

private:
    int Remaining() const {
        LONGLONG left = (LONGLONG)end_ - (LONGLONG)GetTickCount64();
        return left > 0 ? (int)left : 0;
    }

    bool SendFrame(const std::vector<BYTE>& f) {
        std::vector<BYTE> r;
        while (Remaining() > 0) {
            if (!d_.Write(f)) return false;
            ULONGLONG resendAt = GetTickCount64() + kHeroResendMs;
            for (;;) {
                LONGLONG wait = std::min<LONGLONG>((LONGLONG)resendAt - (LONGLONG)GetTickCount64(), Remaining());
                if (wait <= 0) break;
                if (!ReadFrame(r, (int)wait)) {
                    if (ioError_) return false;
                    break;
                }
                if (IsAckFor(r, f)) return true;
            }
        }
        return false;
    }

    // Reads one HERO frame, acknowledges keyboard data/trailer frames and picks up the battery.
    // Returns false on timeout or I/O error.
    bool ReadFrame(std::vector<BYTE>& r, int waitMs) {
        for (;;) {
            if (waitMs <= 0) return false;
            ULONGLONG before = GetTickCount64();
            int n = d_.Read(r, waitMs);
            int took = (int)(GetTickCount64() - before);
            // A read that fails at once is an I/O error (receiver unplugged), not a timeout: stop.
            if (n < 0 && took + 20 < waitMs) {
                ioError_ = true;
                return false;
            }
            if (n < 0) return false;
            waitMs -= took;
            if (n < 20 || r[0] != kHeroId || r[1] != kHeroMagic) continue;  // unrelated report
            BYTE type = r[4] & 0x7F;
            if (type == kHeroData) {
                if (r[19] != SumRange(r, 1, 19)) continue;  // damaged: no ACK, the keyboard resends
                d_.Write(HeroAck(r));
                if ((r[3] & 0x7F) == 1) Inspect(r);
            } else if (type == kHeroTrailerDev) {
                d_.Write(HeroAck(r));
                if (inReply_) replyDone_ = true;
                inReply_ = false;
            }
            return true;
        }
    }

    // First data frame of a keyboard message: payload starts at byte 5.
    void Inspect(const std::vector<BYTE>& r) {
        inReply_ = false;
        if (r[5] == 0x87 && r[11] >= 1 && r[11] <= 100) {  // reply to our request
            level_ = r[11];
            flags_ = r[12];
            inReply_ = true;
        } else if (r[5] == 0xFE && r[6] == 0x05 && r[7] >= 1 && r[7] <= 100 && level_ < 0) {
            level_ = r[7];  // pushed by the keyboard on its own; keep waiting for the real reply
            flags_ = r[8];
        }
    }

    HidDev& d_;
    ULONGLONG end_;
    int level_ = -1, flags_ = -1;  // flags: the byte after the level, 0x10 while charging
    bool inReply_ = false, replyDone_ = false, ioError_ = false;
};

bool QueryHeroBattery(HidDev& d, int& level, bool& charging) {
    std::vector<BYTE> msg(56, 0);
    msg[0] = 0x09;
    msg[1] = 0x87;  // battery
    msg[4] = 0x01;
    d.Flush();
    HeroLink link(d);
    if (!link.SendMessage(msg)) return false;
    if (!link.WaitForBattery()) return false;
    level = link.level();
    charging = link.charging();
    return true;
}

// Other keyboards share these receiver ids: the Redragon K673 comes on 3554:FA09 as well, ignores
// these frames, and every request wakes it right after it has gone to sleep (issue #191). A
// receiver that has never answered since it was plugged in is therefore asked less and less often
// once it has stayed silent for a while (5, 10, 20, 40 minutes, then hourly). One answer brings
// back the normal per-poll query; unplugging the receiver starts over. So does a keyboard on this receiver id
// that had readings in the battery history before the app started: an AULA keyboard that was asleep or
// switched off through the first minutes after boot was otherwise asked only every 10 to 60 minutes, and
// stayed missing from the tray for most of an hour although it answered right away when asked.
struct ReceiverState {
    bool answered = false;
    int misses = 0, backoffs = 0;
    ULONGLONG firstMiss = 0, nextTry = 0;
};
constexpr int kMissesBeforeBackoff = 3;
constexpr ULONGLONG kBackoffAfterMs = 5 * 60 * 1000;  // keep asking for 5 minutes: the keyboard may be switched on late
constexpr ULONGLONG kFirstBackoffMs = 5 * 60 * 1000;
constexpr ULONGLONG kMaxBackoffMs = 60 * 60 * 1000;

// --- SC580 / SC580X mouse (cable 088D:062E, receiver 089D:062F) ---

struct MouseId {
    USHORT vid, pid;
    bool cable;
};
const MouseId kSc580[] = { { 0x088D, 0x062E, true }, { 0x089D, 0x062F, false } };

constexpr BYTE kScGetInfo = 0x30;
constexpr BYTE kScOk = 0, kScBusy = 1, kScOffline = 2, kScDataError = 3;
constexpr int kScDeadlineMs = 1000;  // the cable answers within a few ms, the receiver within ~100 ms
constexpr int kScResendMs = 250;

enum class ScResult { Ok, Offline, Failed };

// Sends 30 00 01 00 ... and waits for an OK reply, resending on busy / stale replies within one deadline.
ScResult Sc580Info(HidDev& d, std::vector<BYTE>& data) {
    std::vector<BYTE> req(33, 0);  // report id 0 + 32-byte payload; the checksum over empty data is 0
    req[1] = kScGetInfo;
    req[3] = 0x01;
    const ULONGLONG end = GetTickCount64() + kScDeadlineMs;
    auto left = [&] { return (int)std::max<LONGLONG>((LONGLONG)end - (LONGLONG)GetTickCount64(), 0); };
    d.Flush();
    std::vector<BYTE> r;
    while (left() > 0) {
        if (!d.Write(req)) return ScResult::Failed;
        ULONGLONG resendAt = GetTickCount64() + kScResendMs;
        bool resend = false;
        while (!resend) {
            int wait = (int)std::min<LONGLONG>((LONGLONG)resendAt - (LONGLONG)GetTickCount64(), left());
            if (wait <= 0) break;
            ULONGLONG before = GetTickCount64();
            int n = d.Read(r, wait);
            // A read that fails at once is an I/O error (device unplugged), not a timeout: stop.
            if (n < 0 && (int)(GetTickCount64() - before) + 20 < wait) return ScResult::Failed;
            if (n < 32) continue;
            if (r[0] == 0xD1 || r[0] == 0xD2) continue;  // pushed power / DPI notifications
            if (r[0] != kScGetInfo) {                   // the receiver's reply to an earlier request
                resend = true;
                continue;
            }
            switch (r[1]) {
            case kScOk:
                if (r[31] != SumRange(r, 4, 31)) continue;
                data.assign(r.begin() + 4, r.begin() + 31);
                return ScResult::Ok;
            case kScOffline:
                return ScResult::Offline;
            case kScBusy:
                Sleep((DWORD)std::min(300, left()));
                resend = true;
                break;
            case kScDataError:
            default:
                resend = true;
                break;
            }
        }
    }
    return ScResult::Failed;
}

void PollSc580(PollContext& ctx) {
    const std::wstring key = L"aula:sc580";  // one entry when the mouse charges on the cable next to its receiver
    for (const auto& m : kSc580) {
        if (ctx.IsClaimed(m.vid, m.pid)) continue;
        bool listed = false;
        for (const auto& o : ctx.out) listed |= o.key == key;
        if (listed) {  // the cable answered first: leave the receiver alone
            ctx.Claim(m.vid, m.pid);
            continue;
        }
        for (const HidColl* c : ctx.Find(m.vid, m.pid, [](const HidColl& c) {
                 return c.page() == 0xFF02 && c.usage() == 0x0000 && c.outLen() >= 33 && c.inLen() >= 33;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            std::vector<BYTE> data;
            ScResult res = Sc580Info(d, data);
            if (res == ScResult::Failed) continue;
            ctx.Claim(m.vid, m.pid);
            // The receiver answers status 2 (and pushes D1 00) as soon as the mouse dozes (seen 20-60 s after
            // it was last moved) or is switched off, and status 0 again right after it wakes. That is reported as an
            // asleep entry so the tray keeps the last level: leaving it out made the mouse disappear once
            // graceSec ran out, e.g. after the PC spent longer than that in Modern Standby.
            bool offline = res == ScResult::Offline || data[10] == 0;
            if (!offline && data[9] > 100) break;
            DeviceStatus s;
            s.key = key;
            s.name = L"AULA SC580";
            s.kind = DeviceKind::Mouse;
            s.via = m.cable ? L"USB cable" : L"2.4 GHz receiver";
            if (offline) {
                s.asleep = true;
            } else if (data[8] == 1) {
                // While charging the mouse reports a fixed 100 (seen going 89 -> 100 the moment the cable
                // went in, and back to 94 when it came out), so the level is unknown: the tray shows the
                // last real level with the charging mark. Charge state 2 (full) keeps its 100.
                s.charging = true;
            } else {
                s.percent = data[9];
            }
            s.graceSec = 1800;  // only used when the receiver stops answering altogether
            ctx.out.push_back(s);
            break;
        }
    }
}

}  // namespace

void PollAula(PollContext& ctx) {
    PollSc580(ctx);

    static ReceiverState states[std::size(kReceivers)];
    for (size_t i = 0; i < std::size(kReceivers); i++) {
        USHORT vid = kReceivers[i].vid, pid = kReceivers[i].pid;
        ReceiverState& st = states[i];
        if (ctx.Find(vid, pid, [](const HidColl&) { return true; }).empty()) st = ReceiverState();  // unplugged
        if (!st.answered && ctx.SeenBefore(L"aula:" + Hex4(vid) + L":" + Hex4(pid))) st.answered = true;
        if (ctx.IsClaimed(vid, pid)) continue;
        if (!st.answered && st.nextTry && GetTickCount64() < st.nextTry) continue;
        bool tried = false, answered = false;
        if (IsAppleId(vid, pid) && !HasMchoseLayout(ctx, vid, pid)) continue;  // a SONiX dongle: sonix.cpp
        for (const HidColl* c : ctx.Find(vid, pid, IsByCollection)) {
            HidDev d;
            if (!d.Open(*c)) continue;
            // The generic receiver names carry a model code (M HUB asks no other 3554 / 41E4 device).
            std::wstring product = HidProduct(*c);
            bool generic = product.find(L"2.4G Wireless Receiver") != std::wstring::npos ||
                           product.find(L"MCHOSE 2.4G Wireless") != std::wstring::npos;
            // M HUB names a 05AC:024F keyboard from its product string ("K87", "K99"), so ask for the
            // model code there whatever the name is.
            const wchar_t* model = generic || IsAppleId(vid, pid) ? IdentifyModel(d) : nullptr;
            bool mchose = vid == 0x41E4 || IsAppleId(vid, pid) || model != nullptr;
            std::vector<BYTE> r;
            tried = true;
            // Byte 2 alternates 00 / 01 on resends; M HUB sends 01, the AULA references 00.
            if (!Exchange(d, 0x4A, r, 4000)) continue;
            int level = r[5];
            if (level < 1 || level > 100) continue;
            answered = true;
            DeviceStatus s;
            s.key = L"aula:" + Hex4(vid) + L":" + Hex4(pid);
            if (model)
                s.name = model;
            else if (IsAppleId(vid, pid) && !generic && !product.empty())
                s.name = product;  // a product string that names the keyboard, as M HUB's getDevName uses
            else
                s.name = mchose ? L"MCHOSE wireless keyboard" : L"AULA wireless keyboard";
            s.kind = DeviceKind::Keyboard;
            s.via = L"2.4 GHz receiver";
            s.percent = level;
            s.charging = (r[6] >> 4) == 1;  // high nibble: AULA's 0x10 cable flag, M HUB's chargeStatus
            // An idle keyboard sometimes misses the 4 s deadline (4 of 15 polls on an F75 that was awake);
            // keep the last value instead of dropping the keyboard until the next poll.
            s.graceSec = 1800;
            ctx.Add(s, vid, pid);
            break;
        }
        if (answered) {
            st.answered = true;
        } else if (tried && !st.answered) {
            ULONGLONG now = GetTickCount64();
            if (!st.misses++) st.firstMiss = now;
            if (st.misses >= kMissesBeforeBackoff && now - st.firstMiss >= kBackoffAfterMs) {
                int doublings = std::min(st.backoffs++, 4);
                st.nextTry = now + std::min(kFirstBackoffMs << doublings, kMaxBackoffMs);
            }
        }
    }

    for (auto [vid, pid] : ctx.Products(0x372E)) {
        if (pid != 0x106C) continue;  // HERO 68 Air "8K Wireless Receiver"; other 372E products are unknown
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF60 && c.usage() == 0x0061 && c.outLen() >= 20 && c.inLen() >= 20;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            int level = -1;
            bool charging = false;
            if (!QueryHeroBattery(d, level, charging)) continue;
            DeviceStatus s;
            s.key = L"aula:" + Hex4(vid) + L":" + Hex4(pid);
            s.name = L"AULA wireless keyboard";
            s.kind = DeviceKind::Keyboard;
            s.via = L"2.4 GHz receiver";
            s.percent = level;
            s.charging = charging;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
