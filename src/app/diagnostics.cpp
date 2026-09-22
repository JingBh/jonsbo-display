// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "app/application.hpp"
#include "integrations/home_assistant.hpp"

namespace jonsbo {
int CheckIntegrations(const std::filesystem::path& config) {
  if (Ini(config, L"home_assistant", L"url").empty()) {
    std::cerr << "Home Assistant: not configured\n";
    return 1;
  }
  HomeAssistant client(config, true);
  const auto deadline = Clock::now() + std::chrono::seconds(20);
  while (Clock::now() < deadline) {
    const auto home = client.Read();
    if (home.connected) {
      int available = 0;
      for (const auto& d : home.lights)
        available += d.available;
      for (const auto& d : home.covers)
        available += d.available;
      for (const auto& d : home.climate)
        available += d.available;
      bool sensors = home.temperature != L"—" && home.humidity != L"—";
      std::cout << "Home Assistant: authenticated; entities " << available << "/7; sensors "
                << (sensors ? "available" : "unavailable") << "\n";
      return available == 7 && sensors ? 0 : 2;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cerr << "Home Assistant: connection check failed\n";
  return 1;
}
}  // namespace jonsbo
