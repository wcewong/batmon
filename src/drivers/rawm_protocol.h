#pragma once
// RAWM (and SADES) mice on Nordic / Panchip receivers, RAWM HUB "Hub Web 2.0" protocol.
// Kept independent of Win32 so captured frames can be checked without a mouse.
//
// Source: RAWM HUB web app (https://www.rawmtech.com/hub.html), hub.miracletek.net/hub/js/library.min.js:
// send_event_query, send_event_ping, read_event, send_client_data, device_receive_data, parse_cmd,
// parse_device_info. Verified against a Leviathan V4 on the RAWM HS Receiver (1915:2346).
//
// Transport: 64-byte unnumbered output / input reports on vendor collection FF00:0001.
//  * Packet byte 0: type (top two bits) | payload size (low six bits). 0x80 = event data,
//    0x40 = buffer-size (flow control), 0xC0 | n = "extended": next byte is a packet for ESB channel n
//    (the mouse behind a receiver is channel 0).
//  * Event data forms a byte stream. Events start with FF FF FF FF (device to host only), then
//    [cmd | ((len >> 4) & 0xF0), len & 0xFF, body...], where len counts the two header bytes.
//  * The device only answers while the host keeps sending packets, so the host pings.
// Commands used here are the ones the app sends on connect: CMD_QUERY (0x01) and CMD_PING (0x0E).
// CMD_QUERY_RESULT (0x02) carries a JSON object; for mice it has "battery" (percent) and "chr"
// (1 = charging). CMD_NOTIFY (0x0B) type 0x17 carries [battery, charging] when it changes.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Rawm {

constexpr std::uint8_t kCmdQuery = 0x01, kCmdQueryResult = 0x02, kCmdNotify = 0x0B, kCmdPing = 0x0E;
constexpr std::uint8_t kNotifyBattery = 0x17;
constexpr std::uint8_t kOsPc = 0x03;
constexpr int kNoChannel = -1;

// Output report (with the leading 0 report ID) carrying one event, optionally for an ESB channel.
inline std::vector<std::uint8_t> Packet(const std::vector<std::uint8_t>& event, int channel = kNoChannel) {
    std::vector<std::uint8_t> p{ 0x00 };
    if (channel >= 0) p.push_back((std::uint8_t)(0xC0 | (channel & 0x3F)));
    p.push_back((std::uint8_t)(0x80 | (event.size() & 0x3F)));
    p.insert(p.end(), event.begin(), event.end());
    return p;
}

// send_event_query: [CMD_QUERY, len, OS_PC, 0, 0, unix seconds (8 bytes, little endian)].
inline std::vector<std::uint8_t> QueryEvent(std::uint64_t unixSeconds) {
    std::vector<std::uint8_t> e{ kCmdQuery, 0, kOsPc, 0, 0 };
    for (int i = 0; i < 8; i++) e.push_back((std::uint8_t)(unixSeconds >> (8 * i)));
    e[1] = (std::uint8_t)e.size();
    return e;
}

// send_event_ping as the app actually transmits it (its sync string collapses to one zero byte).
inline std::vector<std::uint8_t> PingEvent() { return { kCmdPing, 4, 0, 0 }; }

struct Status {
    bool haveInfo = false;      // a QUERY_RESULT was parsed
    std::string json;           // its JSON text
    int percent = -1;           // 0..100 when known
    bool charging = false;
};

// Integer value of "key": in a flat JSON object; false when missing.
inline bool JsonInt(const std::string& json, const char* key, long& out) {
    std::string k = std::string("\"") + key + "\":";
    size_t p = json.find(k);
    if (p == std::string::npos) return false;
    const char* s = json.c_str() + p + k.size();
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s) return false;
    out = v;
    return true;
}

inline std::string JsonString(const std::string& json, const char* key) {
    std::string k = std::string("\"") + key + "\":\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return {};
    p += k.size();
    size_t e = json.find('"', p);
    return e == std::string::npos ? std::string() : json.substr(p, e - p);
}

// Reassembles the event stream of one channel and decodes the events of interest.
class Stream {
public:
    void Append(const std::uint8_t* data, size_t size) {
        buf_.insert(buf_.end(), data, data + size);
        if (buf_.size() > 8192) buf_.erase(buf_.begin(), buf_.end() - 4096);
        Parse();
    }
    const Status& status() const { return st_; }

private:
    void Parse() {
        size_t o = 0;
        while (o + 6 <= buf_.size()) {
            if (!(buf_[o] == 0xFF && buf_[o + 1] == 0xFF && buf_[o + 2] == 0xFF && buf_[o + 3] == 0xFF)) {
                o++;
                continue;
            }
            size_t h = o + 4;
            unsigned cmd = buf_[h] & 0x0F;
            size_t len = ((buf_[h] << 4) & 0xF00) | buf_[h + 1];
            if (len < 2) { o = h; continue; }
            if (h + len > buf_.size()) break;  // wait for the rest
            const std::uint8_t* body = buf_.data() + h + 2;
            size_t n = len - 2;
            if (cmd == kCmdQueryResult) OnInfo(body, n);
            else if (cmd == kCmdNotify && n >= 3 && body[0] == kNotifyBattery && body[1] <= 100) {
                st_.percent = body[1];
                st_.charging = body[2] == 1;
            }
            o = h + len;
        }
        buf_.erase(buf_.begin(), buf_.begin() + o);
    }
    void OnInfo(const std::uint8_t* body, size_t n) {
        std::string s(reinterpret_cast<const char*>(body), n);
        size_t z = s.find('\0');
        if (z != std::string::npos) s.resize(z);
        if (s.empty() || s.front() != '{') return;
        st_.haveInfo = true;
        st_.json = s;
        long v;
        if (JsonInt(s, "battery", v) && v >= 0 && v <= 100) st_.percent = (int)v;
        if (JsonInt(s, "chr", v)) st_.charging = v == 1;
    }
    std::vector<std::uint8_t> buf_;
    Status st_;
};

// Routes one input report (report ID already stripped) to the receiver stream or a channel stream.
struct Link {
    Stream direct;   // the USB device itself (receiver, or a mouse on its cable)
    Stream channel0; // the mouse behind a receiver
    void Feed(const std::uint8_t* r, size_t size) {
        if (!size) return;
        Stream* s = &direct;
        if ((r[0] & 0xC0) == 0xC0) {
            if ((r[0] & 0x3F) != 0) return;
            s = &channel0;
            r++;
            size--;
            if (!size) return;
        }
        if ((r[0] & 0xC0) != 0x80) return;  // flow control / raw packets
        size_t n = r[0] & 0x3F;
        if (n > size - 1) n = size - 1;
        s->Append(r + 1, n);
    }
};

}  // namespace Rawm
