#pragma once
#include <windows.h>
#include <string>
#include "update_core.h"

namespace updates {
// Controller entry points are called on the UI thread. Worker results are copied
// under a mutex; the worker never owns a window or posts heap-allocated messages.
void Initialize();
void Tick(HWND owner);
void Shutdown();
std::wstring MenuLabel();
bool Busy();
bool AutomaticEnabled();
bool Supported();
void ToggleAutomatic();
void Activate(HWND owner);

// Native helpers also used by the download worker and temporary installer helper.
struct Installation {
    std::wstring directory;
    bool allUsers = false;
    bool valid = false;
};
Installation FindInstallation(const std::wstring& executable);
std::wstring ExecutablePath();
bool VerifyFile(const std::wstring& file, const std::string& expectedSha256, std::uint64_t expectedSize);
// The app's own update folder, %LOCALAPPDATA%\WirelessBattery\updates (created when missing), or empty when it
// is unavailable or a reparse point. Each download gets a random 32-hex-digit subfolder there.
std::wstring DownloadRoot();
}
