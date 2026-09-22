// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
class QuotaMotion {
 public:
  void Set(double value, double now, bool snap = false) {
    value = std::clamp(value, 0., 100.);
    if (!initialized || snap) {
      from = to = value;
      started = now;
      initialized = true;
      return;
    }
    if (std::abs(value - to) < .0001)
      return;
    from = At(now);
    to = value;
    started = now;
  }
  double At(double now) const {
    double t = std::clamp((now - started) / .65, 0., 1.);
    return from + (to - from) * (1 - std::pow(1 - t, 3.));
  }
  bool Active(double now) const {
    return initialized && std::abs(to - from) > .0001 && now - started < .65;
  }

 private:
  double from = 0, to = 0, started = 0;
  bool initialized = false;
};

}  // namespace jonsbo
