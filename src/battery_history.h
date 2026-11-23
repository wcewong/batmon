#pragma once
#include <windows.h>
#include <set>
#include <string>
#include <vector>

namespace battery_history {
// Called once before polling starts. Demo data is ephemeral and never written to disk.
void Initialize(bool demo);
// Keys that had readings in the history file when Initialize() ran. Filled once before polling starts and
// never changed afterwards, so the poll thread may read it.
const std::set<std::wstring>& KeysAtStartup();
// Stores only valid, connected and awake readings (the caller enforces connection state).
void Observe(const std::wstring& key, int percent, bool charging);
// The same conservative time-to-empty the viewer shows, in hours, or -1 when there is none (charging, too
// few or stale readings); RemainingText formats it ("~3 d 4 h") or returns an empty string.
double HoursRemaining(const std::wstring& key);
std::wstring RemainingText(const std::wstring& key);
// A rough time-to-empty for the hover card while the estimate above is not ready yet: the current discharge
// from as little as 1 hour and 1 percentage point of decline. Always shown as approximate; never used for
// notifications. -1 when there is not even that.
double RoughHoursRemaining(const std::wstring& key);
std::wstring FormatHours(double hours);
// The recorded readings of the last `hours`, oldest first, for the small chart in the hover card.
// secondsAgo is negative (0 = now); nothing is interpolated.
struct Point { long long secondsAgo; int percent; bool charging; };
std::vector<Point> Recent(const std::wstring& key, int hours);
// Empty when the conservative runtime forecast is unavailable; the UI should display % instead.
std::wstring EstimatedRuntimeText(const std::wstring& key);
// Non-modal native Win32 history viewer; device identity remains the original key.
void Show(HWND owner, const std::wstring& key, const std::wstring& displayName);
// The viewer's "Estimated time to empty" as text ("~2 d 5 h"), or empty when there is none.
std::wstring TimeToEmpty(const std::wstring& key);
} // namespace battery_history
