// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "device/display_session.hpp"
#include <tlhelp32.h>

namespace jonsbo {
class DisplayTransport {
 public:
  void Start();
  void Submit(const Frame& frame);
  std::wstring Status();
  std::atomic<uint64_t> accepted{0}, rejected{0};

 private:
  std::mutex mutex;
  Frame latest;
  uint64_t version = 0;
  std::wstring status;
  std::jthread worker;
};

}  // namespace jonsbo
