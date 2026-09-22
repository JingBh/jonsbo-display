// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "integrations/codex_executable.hpp"
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>

namespace jonsbo {
std::wstring CodexExecutable(const std::filesystem::path& config) {
  auto p = Ini(config, L"codex", L"executable");
  if (!p.empty())
    return p;
  wchar_t found[32768];
  if (SearchPathW(nullptr, L"codex.exe", nullptr, 32768, found, nullptr))
    return found;
  // Prefer the desktop app's extracted bundled runtime: WindowsApps can deny direct execution.
  wchar_t local[32768];
  if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) {
    std::filesystem::path root = std::filesystem::path(local) / L"OpenAI/Codex/bin";
    std::error_code error;
    std::filesystem::path newest;
    std::filesystem::file_time_type time{};
    for (const auto& folder : std::filesystem::directory_iterator(root, error)) {
      auto candidate = folder.path() / L"codex.exe";
      if (std::filesystem::is_regular_file(candidate, error)) {
        auto modified = std::filesystem::last_write_time(candidate, error);
        if (newest.empty() || modified > time) {
          newest = candidate;
          time = modified;
        }
      }
    }
    if (!newest.empty())
      return newest.wstring();
  }
  // Enumerate registered packages instead of hard-coding a WindowsApps version.
  try {
    winrt::Windows::Management::Deployment::PackageManager manager;
    for (const auto& package : manager.FindPackagesForUser(L"")) {
      auto name = package.Id().Name();
      if (name != L"OpenAI.Codex" && name != L"OpenAI.ChatGPT")
        continue;
      std::filesystem::path root(package.InstalledLocation().Path().c_str());
      for (auto relative : {L"app/resources/codex.exe", L"resources/codex.exe"}) {
        auto candidate = root / relative;
        if (std::filesystem::is_regular_file(candidate))
          return candidate.wstring();
      }
    }
  } catch (...) {
  }
  return L"codex.exe";
}
std::wstring ResetCountdown(int64_t timestamp) {
  auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  if (timestamp <= now)
    return L"等待刷新";
  auto minutes = (timestamp - now + 59) / 60;
  return minutes >= 1440 ? std::to_wstring(minutes / 1440) + L"天" +
                               std::to_wstring(minutes / 60 % 24) + L"小时后重置"
                         : std::to_wstring(minutes / 60) + L"小时" + std::to_wstring(minutes % 60) +
                               L"分钟后重置";
}

}  // namespace jonsbo
