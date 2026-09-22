// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
struct NumericPanelMetrics {
  float cpuPercent = 0;
  std::optional<float> cpuTemperature;
  std::optional<float> gpuTemperature;
  std::optional<float> cpuFrequencyMhz;
};

std::array<uint8_t, 64> EncodeNumericPanelReport(const NumericPanelMetrics& metrics,
                                                  const SYSTEMTIME& time);

class NumericPanelTransport {
 public:
  ~NumericPanelTransport();
  void Start();
  void Restart();
  void Update(const NumericPanelMetrics& metrics);
  std::wstring Status();
  std::atomic<uint64_t> accepted{0};

 private:
  void Run(std::stop_token stop);
  std::mutex mutex;
  NumericPanelMetrics metrics;
  std::wstring status;
  std::jthread worker;
};
}  // namespace jonsbo
