// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "integrations/system_media.hpp"

namespace jonsbo {
class HomePodMedia {
 public:
  HomePodMedia();
  ~HomePodMedia();
  void SetActive(bool value);
  MediaState Read();

 private:
  void Run(std::stop_token stop);
  std::mutex mutex;
  std::condition_variable_any wake;
  bool active = false, refresh = false;
  MediaState state;
  std::jthread worker;
};
}  // namespace jonsbo
