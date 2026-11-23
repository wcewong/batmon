#pragma once
// Portable display-name preferences. The device's stable key and driver-provided
// name remain unchanged; aliases affect presentation only.
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aliases {
using AliasMap = std::map<std::wstring, std::wstring>;
constexpr size_t kMaxNameLength = 64;
constexpr size_t kMaxDeviceKeyLength = 512;
constexpr size_t kMaxAliases = 128;

inline bool Space(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

inline std::wstring Trim(const std::wstring& name) {
    size_t first = 0, last = name.size();
    while (first < last && Space(name[first])) ++first;
    while (last > first && Space(name[last - 1])) --last;
    return name.substr(first, last - first);
}

inline bool ValidName(const std::wstring& name) {
    if (name.empty() || name.size() > kMaxNameLength) return false;
    for (wchar_t c : name)
        if (c < 32 || c == 127) return false;
    return true;
}

inline std::wstring DisplayName(const AliasMap& names, const std::wstring& key, const std::wstring& original) {
    auto it = names.find(key);
    return it == names.end() ? original : it->second;
}

// REG_MULTI_SZ: key\0alias\0key\0alias\0\0. No lossy key hashing or
// delimiter escaping, so multiple devices and Unicode names round-trip.
inline std::vector<wchar_t> Encode(const AliasMap& names) {
    std::vector<wchar_t> data;
    size_t count = 0;
    for (const auto& [key, value] : names) {
        if (count >= kMaxAliases) break;
        if (key.empty() || key.size() > kMaxDeviceKeyLength || !ValidName(value)) continue;
        if (key.find(L'\0') != std::wstring::npos) continue;
        data.insert(data.end(), key.begin(), key.end());
        data.push_back(0);
        data.insert(data.end(), value.begin(), value.end());
        data.push_back(0);
        ++count;
    }
    data.push_back(0);
    if (data.size() == 1) data.push_back(0);
    return data;
}

inline AliasMap Decode(const wchar_t* data, size_t count) {
    AliasMap names;
    if (!data || count < 2 || data[count - 1] != 0 || data[count - 2] != 0) return names;
    const wchar_t* p = data;
    const wchar_t* end = data + count;
    while (p < end && *p && names.size() < kMaxAliases) {
        auto keyEnd = std::find(p, end, L'\0');
        if (keyEnd == end) break;
        std::wstring key(p, keyEnd);
        p = keyEnd + 1;
        if (p == end || !*p) break;
        auto valueEnd = std::find(p, end, L'\0');
        if (valueEnd == end) break;
        std::wstring name(p, valueEnd);
        p = valueEnd + 1;
        if (!key.empty() && key.size() <= kMaxDeviceKeyLength && ValidName(name))
            names.emplace(std::move(key), std::move(name));
    }
    return names;
}
}  // namespace aliases
