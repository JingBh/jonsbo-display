// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
struct ApplicationOptions {
  bool usb = false, reduced = false, checkIntegrations = false;
  int tab = 3, seconds = 0;
  std::filesystem::path assets, config;
};
int RunApplication(ApplicationOptions options);
int CheckIntegrations(const std::filesystem::path& config);

}  // namespace jonsbo
