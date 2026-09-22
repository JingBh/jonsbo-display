// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
using Clock = std::chrono::steady_clock;
void Check(HRESULT hr);
float Seconds(Clock::time_point time);
double ClockSeconds();
std::filesystem::path ExeDir();
std::wstring Ini(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key,
                 const wchar_t* fallback = L"");
std::wstring Fixed(double number, int digits = 0);
std::wstring BitRate(double bytesPerSecond);
std::string ToUtf8(const std::wstring& value);

}  // namespace jonsbo
