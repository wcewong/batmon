#pragma once
#include "../build/version.h"

namespace appversion {
constexpr const char* text = WB_VERSION_STRING;
constexpr const wchar_t* wide = WB_VERSION_WSTRING;
constexpr bool official = WB_OFFICIAL_RELEASE != 0;
}
