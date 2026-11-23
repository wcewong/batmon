#pragma once
#include <windows.h>

// Smallest and largest custom taskbar positions, in DPI-independent pixels from the notification area.
// Negative values move the widget towards (or over) the notification area.
constexpr int kMinTaskbarOffset = -1000;
constexpr int kMaxTaskbarOffset = 3000;

// Native, modal Win32 editor for a custom taskbar position. Returns false if the user cancels.
bool PromptTaskbarOffset(HWND owner, int current, int& result);
