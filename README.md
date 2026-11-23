# Batmon

Batmon is a macOS menu-bar app that shows the battery level of your wireless devices:
Bluetooth and 2.4 GHz mice, keyboards, headsets, controllers, earbuds, and more. Each device
appears as its own menu-bar item with an icon and battery percent, in the style of the Windows
app it is ported from.

It is a macOS port of [krisgfx9/Wireless-Device-Battery-Monitor](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor)
(GPL v3). The project's 146 vendor battery-protocol drivers compile unchanged; macOS supplies its
own hardware backends in place of the upstream Windows and Linux ones. See [NOTICE.md](NOTICE.md).

## Features

- One menu-bar item per device, with a kind-specific icon and a battery percent colored by level
  (green, orange, red).
- Per-device menu: battery detail, transport, hide, icon picker, refresh, and quit.
- Show or hide any device from a control item, with the choice remembered across launches.
- Per-device icon override (mouse, keyboard, knob/dial, headphones, controller, speaker,
  microphone), also remembered across launches.
- Refresh on wake, so batteries reappear the moment the Mac wakes rather than after the next tick.
- Devices stay visible even when the system has no battery value yet (shown as a question mark),
  then fill in once the level is read.

## How it reads batteries

Batmon combines four sources and deduplicates the results by device name:

| Source | Covers |
|--------|--------|
| HID vendor drivers (`src/drivers`, via IOKit `IOHIDManager`) | Logitech HID++, Razer, SteelSeries, Corsair, HyperX, Glorious, Kysona/Pulsar/ATK, Keychron, Lamzu, Ninjutso, Rapoo, CoolerMaster, Finalmouse, Zebronics, PlayStation, and more as drivers are enabled |
| CoreBluetooth Battery Service (0x180F / 0x2A19), in Swift | Standard BLE devices the system owns (keyboards, knobs, earbuds) whose GATT battery macOS does not otherwise expose |
| `system_profiler SPBluetoothDataType`, parsed in-process | Any Bluetooth device with a cached level; also keeps a device visible right after waking |
| IORegistry `BatteryPercent` | Apple Magic Mouse, Trackpad, and Keyboard |

A note on CoreBluetooth: standard BLE HID devices are owned by the system Bluetooth stack, so their
GATT battery service must be read asynchronously on the main queue (the reader lives in
`app/BLEReader.swift`). The C++ poll returns quickly and the fresh BLE level is merged on top a
moment later.

## Architecture

```
src/
  drivers/            146 upstream vendor drivers, compiled unchanged
  hid.h               shared HID interface (small __APPLE__ branch added)
  platform_compat.h   Windows type shims (small __APPLE__ branch added)
  devices.h           PollAllDevices() contract and DeviceStatus
  mac/
    hid_mac.mm        IOKit IOHIDManager backend implementing hid.h
    ble_mac.mm        system_profiler and IORegistry battery sources
    devices_mac.cpp   driver orchestration for the macOS build
    batcore_bridge.*  pure-C bridge over PollAllDevices() for Swift
  cli/
    main_cli.cpp      command-line poll harness
app/
  BatmonApp.swift         menu-bar app entry, wake hook
  BatteryModel.swift      polling, BLE merge, device state
  StatusBarController.swift  per-device NSStatusItems, menus, hide and icon overrides
  BLEReader.swift         CoreBluetooth battery reader
  Info.plist
```

Almost all of the port is three files: `hid_mac.mm` (IOKit HID transport), `ble_mac.mm` plus
`BLEReader.swift` (Bluetooth and Apple battery sources), and the Swift menu-bar UI. The 146 vendor
drivers and the shared core compile unchanged apart from two small `__APPLE__` branches.

## Build

Requires Xcode command-line tools (clang, Swift, and the IOKit, CoreBluetooth, and Foundation
SDKs). Apple Silicon.

```sh
make -f Makefile.mac app      # build build/Batmon.app (ad-hoc signed)
open build/Batmon.app
```

Command-line harness, for development and for adding device support:

```sh
make -f Makefile.mac cli
./build/batmon-cli            # one poll, prints every device and battery percent
./build/batmon-cli --dump     # every HID collection IOKit sees
BATMON_DEBUG=1 ./build/batmon-cli   # verbose HID and Bluetooth trace
```

## macOS permissions

- Input Monitoring: required for raw HID access to some devices. System Settings, Privacy and
  Security, Input Monitoring, then enable Batmon. For the CLI, grant it to your terminal.
- Bluetooth: required for the CoreBluetooth battery read. The app requests it on first launch
  (Info.plist carries the usage description).

## Distribution

The app is ad-hoc signed, so it runs without an Apple Developer account. Other users will see an
"unidentified developer" prompt on first open and must right-click the app and choose Open once, or
remove the quarantine attribute:

```sh
xattr -dr com.apple.quarantine Batmon.app
```

Notarization (for a friction-free install) would need a paid Apple Developer account.

## License

GPL v3, as a derivative of the upstream project. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
