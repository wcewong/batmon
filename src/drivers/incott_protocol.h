#pragma once
// Incott status decoding (PixArt 093A:522C dongle / 622C cable, vendor collection FF05:0001), kept free of
// Win32 so tests can feed captured frames. Byte indexes below include the report id 09 at [0].
//  * Input report 09 is the status the mouse itself pushes, every few seconds while it is awake. Packed
//    layout (firmware 1.3.8 / 1.4.x): [1] percent | charging << 7, [2] rate | dpi tier << 4,
//    [3] debounce | slot << 6, [4..6] current DPI, [7] motion sync | LOD << 4. Older firmware behind a 1K
//    receiver forwards a 5-byte status instead: [1] charging (non-zero), [2] percent, [3] dpi tier,
//    [4] rate, rest 0. Both layouts and the way to tell them apart are from github.com/oexi/incott-driver-desktop
//    (src/protocol/device.js statusLayoutOf / parseInputReport, from the official desktop driver).
//  * Feature reply 09 89 (status query): [2] percent | charging << 7, [8] = 5A. Over the cable the mouse
//    answers it live. A receiver does not forward 89 to the mouse and answers it itself (oexi, firmware
//    1.4.0.3); on a first-generation G23 Pro behind its 8K receiver the reply stayed "09 89 3a 00 00 3b 00 00 5a"
//    (58%) for 43 hours and across a PC restart while the mouse pushed 73%, its byte 5 (dpi tier in the
//    official layout) did not match the live tier 4, and the reply was the same after 8F, 8E and 83 reads.
//    So on an 8K receiver only the pushed status is used.
//  * Feature reply 09 8F (device info): [2] = 01, [3] = model, [4] = receiver kind (2 = 8K receiver).

#include <cstdint>
#include <vector>

namespace incott_protocol {

using Bytes = std::vector<std::uint8_t>;

enum class Layout { Unknown, Packed, Old };

struct Status {
    int percent = -1;
    bool charging = false;
};

// Decodes a pushed status (input report 09). `layout` is the layout settled on by earlier frames of the same
// device and is updated when this frame is unambiguous. Returns false for frames that fit neither layout.
inline bool ParseStatusPush(const Bytes& r, Layout& layout, Status& out) {
    if (r.size() < 8 || r[0] != 0x09) return false;
    const std::uint8_t* d = r.data() + 1;
    if (!(d[0] | d[1] | d[2] | d[3] | d[4] | d[5] | d[6])) return false;  // all zero: carries nothing
    bool packed = (d[0] & 0x7F) <= 100 && (d[1] & 0x0F) <= 6 && (d[1] >> 4) <= 5 && (d[2] & 0x3F) <= 30;
    bool old = d[0] <= 1 && d[1] <= 100 && d[2] <= 5 && d[3] <= 6 && !d[5] && !d[6];
    if (!packed && !old) return false;
    Layout use;
    if (packed != old) {
        use = packed ? Layout::Packed : Layout::Old;
        layout = use;
    } else {
        // Both fit: packed would mean a 0-1% battery still in use, so take the old layout unless settled.
        use = layout != Layout::Unknown ? layout : Layout::Old;
    }
    if (use == Layout::Old) {
        out.charging = d[0] != 0;
        out.percent = d[1];
    } else {
        out.charging = (d[0] & 0x80) != 0;
        out.percent = d[0] & 0x7F;
    }
    return true;
}

// Feature reply to 09 89. A reply with bytes 2 and 3 both 0 carries nothing (receiver without data).
inline bool ParseStatusFeature(const Bytes& r, Status& out) {
    if (r.size() < 4 || r[0] != 0x09 || r[1] != 0x89 || (r[2] | r[3]) == 0) return false;
    int pct = r[2] & 0x7F;
    if (pct > 100) return false;
    out.percent = pct;
    out.charging = (r[2] & 0x80) != 0;
    return true;
}

enum class Receiver { Unknown, Other, EightK };

// Feature reply to 09 8F. Unknown when the reply is not a device-info echo.
inline Receiver ParseReceiverKind(const Bytes& r) {
    if (r.size() < 5 || r[0] != 0x09 || r[1] != 0x8F || r[2] != 0x01) return Receiver::Unknown;
    return r[4] == 2 ? Receiver::EightK : Receiver::Other;
}

struct Reading {
    bool show = false;       // false: add no entry this poll
    int percent = -1;        // -1: unknown this poll (the app shows the last known value)
    bool charging = false;
    bool asleep = false;     // the receiver answers but the mouse sends nothing
    bool connected = true;
};

// Picks the reading for one Incott PID. `push` is the newest pushed status seen since the previous poll
// (percent -1 when none), `feature` the 89 reply (percent -1 when none or not asked).
inline Reading Choose(bool cable, Receiver receiver, const Status& push, const Status& feature) {
    Reading out;
    const Status* src = nullptr;
    if (push.percent >= 0) src = &push;
    else if (feature.percent >= 0 && (cable || receiver != Receiver::EightK)) src = &feature;
    if (src) {
        out.show = true;
        out.charging = src->charging;
        // A push only comes from a mouse that is on, so its 0% is a real (empty) reading. From a receiver's
        // 89 answer, 0 means the mouse is off or out of range, as before.
        out.connected = src == &push || cable || src->percent > 0 || src->charging;
        if (out.connected) out.percent = src->percent;
        return out;
    }
    if (!cable && receiver == Receiver::EightK) {
        // The 8K receiver answered, but the mouse pushed nothing since the last poll: asleep, switched off or
        // on its cable. Its 89 answer is not the mouse's battery, so no percent is shown.
        out.show = true;
        out.asleep = true;
    }
    return out;
}

}  // namespace incott_protocol
