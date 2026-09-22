// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
struct ApplicationOptions {
  bool usb = true, reduced = false, checkIntegrations = false;
  int tab = 3, seconds = 0;
  std::filesystem::path config;
};
int RunApplication(ApplicationOptions options);
int CheckIntegrations(const std::filesystem::path& config);
int CheckHardwareMonitor();

}  // namespace jonsbo
