#pragma once
// A small transparent strip on the taskbar, just left of the notification area, showing
// "[icon] 30%  [icon] 93%". Implemented as a layered child window of the taskbar (Shell_TrayWnd), with a
// copy on each secondary-monitor taskbar (Shell_SecondaryTrayWnd) next to its clock.
#include <windows.h>
#include <string>
#include <vector>
#include "brands.h"
#include "devices.h"

struct WidgetItem {
    DeviceKind kind = DeviceKind::Other;
    int  percent  = -1;     // -1 = unknown
    std::wstring text;      // shown instead of the percentage when set (coarse levels like "Low")
    bool charging = false;
    bool full     = false;  // on power and fully charged: a green check mark instead of the bolt
    bool dim      = false;  // asleep / stale value
    bool low      = false;
    bool bluetooth = false;  // picks the Bluetooth icon when the device type is unknown
    bool wirelessLink = false;  // on a 2.4 GHz receiver: small wireless mark after the level (opt-in, PR #2277)
    std::wstring label;      // tiny caption on the icon; set only when two devices of one kind are shown
    Accent accent  = Accent::None;  // brand color for the icon, its glow and the level bar
    // Shown in the card that opens when the mouse rests on this device in the strip.
    std::wstring name;               // display name (custom name when renamed)
    std::wstring status;             // "49%, charging", "asleep" ...
    std::vector<std::wstring> info;  // extra lines: estimated time to empty, connection
    // Recorded levels of the last 24 hours for the card's small chart: {seconds ago (<= 0), percent}.
    std::vector<std::pair<long long, int>> trend;
    double hoursLeft = -1;      // estimated time to empty (the chart's dashed forecast), -1 = none
    std::wstring hoursLeftText; // the same, formatted ("~3 d 4 h")
};

namespace widget {
// Experimental MyDockFinder host (PR #251): the strip follows MyFinder's right-hand system indicators
// instead of sitting on the Windows taskbar. Destroys the current strip; Create() makes the new one.
void SetMyFinder(bool on);
// The Segoe Fluent Icons glyph the strip uses for a kind of device (also used by the menu).
wchar_t DeviceGlyph(DeviceKind kind, bool bluetooth);
// Creates the widget on the primary taskbar. Clicks are reported by posting `clickMsg` to `notifyWnd`.
bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme);
void Destroy();
bool Alive();
void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip);
// The card shown when the mouse rests on a device (on by default), and its 24-hour chart (on by default).
// With the card off, the summary is shown as a plain tooltip instead.
void SetCardOptions(bool card, bool chart);
void SetLightTheme(bool light);
// Draws a rounded plate in the taskbar's colours behind the text (kept across Create/Destroy).
void SetBackground(bool on);
// Icon above the percentage instead of beside it, when the taskbar is tall enough (kept across Create/Destroy).
void SetStacked(bool on);
// Include '%' after numeric battery levels (kept across Create/Destroy).
void SetShowPercentSign(bool on);
// Device icon inside a ring that fills with the charge, instead of icon + percentage (kept across Create/Destroy).
void SetRingStyle(bool on);
// Small wireless mark after the level of devices on a 2.4 GHz receiver (kept across Create/Destroy).
void SetConnectionIcons(bool on);
// The strip's content as one line of text (glyphs from Segoe Fluent Icons), for `--text`.
std::wstring ItemsToText(const std::vector<WidgetItem>& items);
// A charging device's icon fills green from the bottom in a loop (kept across Create/Destroy).
void SetAnimateCharging(bool on);
// Steps past other apps' widgets in the taskbar (e.g. a network graph or a countdown) that sit where the
// strip would go, so both stay readable (kept across Create/Destroy).
void SetMakeRoom(bool on);
// Where the column sits on a left / right taskbar (kept across Create/Destroy).
// Auto: above the tray, at the top while task buttons reach it. Bottom (Windows 11): at the very bottom,
// with the taskbar's own content (task buttons and tray) moved up to make room.
enum class VerticalPlace { Auto, Top, AboveTray, Bottom };
void SetVerticalPlace(VerticalPlace place);
// True while the widget is on a left / right taskbar.
bool Vertical();
// At the left end of a Windows 11 taskbar (right of the Widgets button) instead of next to the clock, where
// there is room; otherwise next to the clock as usual (kept across Create/Destroy).
void SetFarLeft(bool on);
// Just left of the taskbar apps (Start / Widgets) on a centred Windows 11 taskbar, where there is room;
// otherwise next to the clock as usual (kept across Create/Destroy).
void SetBeforeApps(bool on);
// Which taskbars show the strip: L"all" (default), L"main", or the id of one other monitor from
// OtherMonitors() (then only that monitor shows it). Kept across Create/Destroy.
void SetMonitorChoice(const std::wstring& choice);
struct MonitorOption {
    std::wstring id;     // lasting monitor identity, stored in the settings
    std::wstring label;  // "Display 3 (right)"
};
// Index of the chosen monitor in `ids` (MonitorOption::id values), or -1 when it is not connected. A saved id
// that matches no monitor exactly still matches the only connected monitor of the same model: a laptop panel's
// id changes when a MUX / Advanced Optimus switch moves it to the other GPU.
int FindMonitor(const std::vector<std::wstring>& ids, const std::wstring& choice);
// The monitors other than the main one that have a horizontal taskbar (empty with one monitor).
std::vector<MonitorOption> OtherMonitors();
// Re-checks the taskbar layout and moves the widget if needed; call about once a second.
// Returns false when the taskbar it was attached to is gone.
bool Reposition();
}
