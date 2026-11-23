#pragma once
#include <windows.h>
#include <string>
#include "update_core.h"

namespace updates {
std::wstring QuoteArgument(const std::wstring& argument);
bool StartApply(HWND owner, const std::wstring& installer, const ReleaseInfo& release, HANDLE* process);
// Returns -1 for a normal launch, otherwise the helper process exit code.
int RunApplyMode(int argc, wchar_t** argv);
}
