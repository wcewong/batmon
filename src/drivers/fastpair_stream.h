#pragma once
// Google Fast Pair Message Stream: the RFCOMM channel (service {df21fe2c-2515-4fdb-8886-f12c4d67927c})
// that Fast Pair devices open next to their audio profiles. Kept independent of Win32 so captured
// bytes can be unit-tested without the device.
//
// Each message: [group][code][length, 2 bytes big-endian][data...]. Messages may be split across or
// share socket reads, so the parser works on everything received so far.
// Group 0x03 (device information event):
//   code 0x01  model id, 3 bytes (big-endian)
//   code 0x02  BLE address updated, 6 bytes: the device's current (rotating) LE address, most
//              significant byte first
// Layout from the Fast Pair specification (developers.google.com/nearby/fast-pair/specifications/
// extensions/messagestream). Checked on a Marshall EMBERTON II: right after the stream opens it sends
// both messages unprompted, and an Android phone then connected over LE to exactly that address.
#include <cstddef>
#include <cstdint>

namespace FastPairStream {

constexpr std::uint8_t kGroupDeviceInfo = 0x03;
constexpr std::uint8_t kCodeModelId = 0x01;
constexpr std::uint8_t kCodeBleAddress = 0x02;

struct Info {
    bool hasModelId = false;
    std::uint32_t modelId = 0;     // 24-bit Fast Pair model id
    bool hasLeAddress = false;
    std::uint64_t leAddress = 0;   // 48-bit, as Windows' BluetoothLEDevice expects it (aa:bb:.. = 0xaabb..)
};

// Reads every complete message in `data` and records the ones above (later ones win). Returns the number
// of bytes consumed by complete messages; a trailing partial message is left for the next call.
inline std::size_t Parse(const std::uint8_t* data, std::size_t size, Info& info) {
    std::size_t pos = 0;
    while (size - pos >= 4) {
        std::size_t len = (std::size_t)data[pos + 2] << 8 | data[pos + 3];
        if (size - pos - 4 < len) break;  // incomplete: wait for more bytes
        const std::uint8_t* p = data + pos + 4;
        if (data[pos] == kGroupDeviceInfo && data[pos + 1] == kCodeModelId && len == 3) {
            info.modelId = (std::uint32_t)p[0] << 16 | (std::uint32_t)p[1] << 8 | p[2];
            info.hasModelId = true;
        } else if (data[pos] == kGroupDeviceInfo && data[pos + 1] == kCodeBleAddress && len == 6) {
            std::uint64_t a = 0;
            for (int i = 0; i < 6; i++) a = a << 8 | p[i];
            info.leAddress = a;
            info.hasLeAddress = a != 0;
        }
        pos += 4 + len;
    }
    return pos;
}

}  // namespace FastPairStream
