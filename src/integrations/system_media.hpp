// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/utilities.hpp"
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

namespace jonsbo {
struct MediaState {
  std::wstring title = L"暂无播放内容", artist = L"等待播放器提供媒体信息", identity;
  std::shared_ptr<std::vector<uint8_t>> artwork;
  bool playing = false;
  double position = 0, duration = 0, rate = 1;
  Clock::time_point sampled = Clock::now();
  uint64_t revision = 0;
  double Position() const;
};
class SystemMedia {
 public:
  explicit SystemMedia(bool enabled) {
    if (enabled)
      worker = std::jthread([this](std::stop_token stop) { Run(stop); });
  }
  MediaState Read();

 private:
  template <class T>
  static auto Await(T operation) {
    if (operation.wait_for(std::chrono::seconds(2)) !=
        winrt::Windows::Foundation::AsyncStatus::Completed) {
      operation.Cancel();
      throw std::runtime_error("Media query timed out");
    }
    return operation.GetResults();
  }
  void Run(std::stop_token stop);
  std::mutex mutex;
  MediaState state;
  std::jthread worker;
};

}  // namespace jonsbo
