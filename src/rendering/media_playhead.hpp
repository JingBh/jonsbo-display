// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
// Smooth an externally driven seek while the target timeline keeps advancing.
// Re-target from the currently displayed position, including rapid reverse seeks.
class MediaPlayhead {
 public:
  void Set(double position, double duration, bool playing, double rate, double now, bool newTrack) {
    double shown = At(now);
    target = position;
    length = std::max(0., duration);
    running = playing;
    speed = rate;
    sampleTime = now;
    offset = newTrack ? 0 : shown - target;
    transitionStart = now;
  }
  double At(double now) const {
    double t = std::clamp((now - transitionStart) / .35, 0., 1.);
    double correction = offset * std::pow(1 - t, 3.);
    return std::clamp(target + (running ? std::max(0., now - sampleTime) * speed : 0) + correction,
                      0., length);
  }
  bool Settling(double now) const {
    return std::abs(offset) > .01 && now - transitionStart < .35;
  }

 private:
  double target = 0, length = 0, speed = 1, sampleTime = 0, offset = 0, transitionStart = 0;
  bool running = false;
};

}  // namespace jonsbo
