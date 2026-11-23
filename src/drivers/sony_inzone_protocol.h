#pragma once
// Sony INZONE "HCI" packets: a Sony command layer shared by every INZONE headset dongle. Kept
// independent of Win32 so captured packets can be checked without the headset.
//
//   command: [01][00 FC][len = 8+n][96 C3][addr][event][type][tid lo][tid hi][payload n][sum]
//            sum = byte sum from 96 (offset 4) to the last payload byte
//   event:   [04][FF][len = 8+n][00][96 C3][addr][event][type][tid lo][tid hi][payload n][sum]
//            sum = byte sum from 00 (offset 3) to the last payload byte
//   addr:    high nibble = destination, low nibble = source (1 = PC, 2 = dongle, 4 = headset)
//   type:    01 GET, 02 SET, 10 RET (reply), 20 / A0 notification
//
// The transport differs per dongle:
//   H9 II, H5, Buds: HID report 02 on usage page FF04, [02][packet length][packet], 64 bytes.
//   H9 / H7 (first generation, 054C:0E53): the dongle's USB serial port, 460800 8N1, bare packets.
//
// Battery = event 04, GET to the headset (addr 0x41); the reply payload is [charging][percent] on
// headsets, percent FF = headset off.
// Packet layout from INZONE Hub 1.0.20 (HciPacket, HciCommandPacket, HciEventPacket,
// SerialPortWrapper, HeadsetParam.BatteryInfo) and github.com/Sapd/HeadsetControl
// (sony_inzone_protocol.hpp). Serial transport checked on an INZONE H9 (first generation).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SonyHci {

constexpr std::uint8_t kBatteryInfo = 0x04;
constexpr std::uint8_t kGet = 0x01;
constexpr std::uint8_t kRet = 0x10;
constexpr std::uint8_t kNotifyActive = 0xA0;

struct Event {
    std::uint8_t id = 0, type = 0;
    std::uint16_t tid = 0;
    std::vector<std::uint8_t> payload;
};

// The 12-byte battery GET command (no payload) from the PC to the headset.
inline std::vector<std::uint8_t> BatteryGet(std::uint16_t tid) {
    std::vector<std::uint8_t> p = { 0x01, 0x00, 0xFC, 8, 0x96, 0xC3, 0x41, kBatteryInfo, kGet,
                                    (std::uint8_t)tid, (std::uint8_t)(tid >> 8), 0 };
    unsigned sum = 0;
    for (std::size_t i = 4; i < p.size() - 1; i++) sum += p[i];
    p.back() = (std::uint8_t)sum;
    return p;
}

// One event packet of n bytes starting at its packet type (04). Only packets addressed to the PC
// with a valid checksum are accepted.
inline bool ParseEvent(const std::uint8_t* f, std::size_t n, Event& e) {
    if (n < 12) return false;
    if (f[0] != 0x04 || f[1] != 0xFF || f[3] != 0x00 || f[4] != 0x96 || f[5] != 0xC3) return false;
    if ((f[6] >> 4) != 0x1) return false;  // must be addressed to the PC
    unsigned sum = 0;
    for (std::size_t i = 3; i < n - 1; i++) sum += f[i];
    if ((std::uint8_t)sum != f[n - 1]) return false;
    e.id = f[7];
    e.type = f[8];
    e.tid = (std::uint16_t)(f[9] | (f[10] << 8));
    e.payload.assign(f + 11, f + n - 1);
    return true;
}

// Takes the next valid event packet off the front of a serial byte stream. Returns false when the
// buffer holds no complete event yet; command packets (the dongle's own traffic), corrupt events
// and stray bytes are dropped on the way.
inline bool NextSerialEvent(std::vector<std::uint8_t>& buf, Event& e) {
    while (!buf.empty()) {
        std::size_t total;
        if (buf[0] == 0x04) {
            if (buf.size() < 3) return false;
            total = 3 + (std::size_t)buf[2];
        } else if (buf[0] == 0x01) {
            if (buf.size() < 4) return false;
            total = 4 + (std::size_t)buf[3];
        } else {
            buf.erase(buf.begin());
            continue;
        }
        if (buf.size() < total) return false;
        bool ok = buf[0] == 0x04 && ParseEvent(buf.data(), total, e);
        buf.erase(buf.begin(), buf.begin() + total);
        if (ok) return true;
    }
    return false;
}

}  // namespace SonyHci
