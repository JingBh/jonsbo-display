// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/dashboard_state.hpp"
#include "integrations/codex_client.hpp"
#include "integrations/codex_executable.hpp"
#include <iphlpapi.h>
#include <pdh.h>
#include <pdhmsg.h>

namespace jonsbo {
inline uint64_t Ticks(FILETIME t) {
  return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
class SystemTelemetry {
 public:
  explicit SystemTelemetry(std::filesystem::path cfg)
      : config(std::move(cfg)), limits(std::make_unique<CodexLimits>(CodexExecutable(config))) {
    worker = std::jthread([this](std::stop_token stop) { Run(stop); });
  }
  DashboardState Read();

 private:
  void Run(std::stop_token stop);
  std::filesystem::path config;
  std::mutex mutex;
  DashboardState state;
  std::unique_ptr<CodexLimits> limits;
  std::jthread worker;
};

}  // namespace jonsbo
