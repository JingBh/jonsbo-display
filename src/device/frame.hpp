// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
inline constexpr int kScreenWidth = 480;
inline constexpr int kScreenHeight = 480;
class Frame {
 public:
  Frame() : pixels_(kScreenWidth * kScreenHeight * 4) {}

  uint8_t* data() {
    return pixels_.data();
  }
  const uint8_t* data() const {
    return pixels_.data();
  }

 private:
  std::vector<uint8_t> pixels_;
};

}  // namespace jonsbo
