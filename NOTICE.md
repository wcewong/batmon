# batmon — attribution & license

**batmon** is a macOS port of **BatteryHub / Wireless-Device-Battery-Monitor** by krisgfx9
(<https://github.com/krisgfx9/Wireless-Device-Battery-Monitor>), licensed under the GNU GPL v3.

Because batmon reuses that project's vendor battery-protocol drivers (`src/drivers/`) and shared
core (`src/hid.h`, `src/platform_compat.h`, `src/devices.h`, …), batmon is a derivative work and is
likewise distributed under the **GNU General Public License v3** (see `LICENSE`).

## What is original to batmon

- `src/mac/hid_mac.mm` — IOKit (`IOHIDManager`) HID backend implementing the `hid.h` interface,
  replacing the Windows Win32 / Linux hidraw backends.
- `src/mac/devices_mac.cpp` — driver orchestration for the macOS build (adapted from the upstream
  Linux `devices_linux.cpp`).
- `src/cli/main_cli.cpp` — command-line poll harness.
- `app/` — SwiftUI menu-bar application (in progress).

## Vendored upstream sources

Everything under `src/` except `src/mac/` and `src/cli/` is vendored from the upstream project and
compiles unchanged, apart from a small `__APPLE__` branch added to `src/hid.h` and
`src/platform_compat.h`.
