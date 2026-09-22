// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
struct DashboardState {
  std::wstring cpu = L"—", cpuDetail = L"采集中…", gpu = L"—", gpuDetail = L"未连接";
  std::wstring disk = L"—", diskDetail = L"采集中…", download = L"—", upload = L"—";
  float cpuPercent = 0;
  std::optional<float> cpuTemperature;
  std::optional<float> gpuTemperature;
  std::optional<float> cpuFrequencyMhz;
  struct Quota {
    std::wstring title, reset;
    double remaining = 0;
  };
  std::vector<Quota> quotas;
  float diskFraction = 0;
  std::array<float, 12> cpuHistory{}, gpuHistory{};
};

inline constexpr wchar_t kWindowClass[] = L"JonsboDisplay.DesktopPanel";
inline const std::array<std::wstring, 5> kPageNames{L"系统", L"房间", L"用量", L"媒体", L"画中画"};
inline const std::array<std::wstring, 5> kPageKeys{L"monitor", L"home", L"codex", L"music",
                                                   L"video"};

}  // namespace jonsbo
