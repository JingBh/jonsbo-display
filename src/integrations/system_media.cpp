// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "integrations/system_media.hpp"

namespace jonsbo {
double MediaState::Position() const {
  return std::clamp(position + (playing ? Seconds(sampled) * rate : 0.), 0.,
                    std::max(0., duration));
}

MediaState SystemMedia::Read() {
  std::lock_guard lock(mutex);
  return state;
}

void SystemMedia::Run(std::stop_token stop) {
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    using namespace winrt::Windows::Media::Control;
    auto manager = Await(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
    std::wstring previousKey, cachedSource;
    uint64_t revision = 0;
    std::shared_ptr<std::vector<uint8_t>> cover;
    GlobalSystemMediaTransportControlsSessionMediaProperties properties{nullptr};
    int metadataTick = 0;
    while (!stop.stop_requested()) {
      try {
        MediaState next;
        auto session = manager.GetCurrentSession();
        // Prefer an actively playing session if the OS's current one is paused.
        if (!session || session.GetPlaybackInfo().PlaybackStatus() !=
                            GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
          for (auto s : manager.GetSessions())
            if (s.GetPlaybackInfo().PlaybackStatus() ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
              session = s;
              break;
            }
        if (session) {
          next.identity = session.SourceAppUserModelId().c_str();
          if (!properties || next.identity != cachedSource || metadataTick++ % 4 == 0) {
            properties = Await(session.TryGetMediaPropertiesAsync());
            cachedSource = next.identity;
          }
          next.title = properties.Title().c_str();
          next.artist = properties.Artist().c_str();
          if (next.title.empty())
            next.title = L"正在播放";
          if (next.artist.empty())
            next.artist = L"未知艺术家";
          std::wstring key = next.identity + L"|" + next.title + L"|" + next.artist + L"|" +
                             properties.AlbumTitle().c_str();
          if (key != previousKey) {
            cover.reset();
            auto thumbnail = properties.Thumbnail();
            if (thumbnail) {
              auto stream = Await(thumbnail.OpenReadAsync());
              if (stream.Size() > 0 && stream.Size() <= 16 * 1024 * 1024) {
                winrt::Windows::Storage::Streams::DataReader reader(stream);
                auto size = Await(reader.LoadAsync(uint32_t(stream.Size())));
                auto bytes = std::make_shared<std::vector<uint8_t>>(size);
                reader.ReadBytes(*bytes);
                cover = bytes;
              }
            }
            previousKey = key;
            ++revision;
          }
          next.artwork = cover;
          auto playback = session.GetPlaybackInfo();
          next.playing = playback.PlaybackStatus() ==
                         GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
          auto rate = playback.PlaybackRate();
          next.rate = rate ? rate.Value() : 1.;
          if (!std::isfinite(next.rate))
            next.rate = 1;
          auto timeline = session.GetTimelineProperties();
          double start = timeline.StartTime().count() / 1e7;
          next.duration = std::max(0., timeline.EndTime().count() / 1e7 - start);
          next.position = timeline.Position().count() / 1e7 - start;
          // Account for age of the OS timeline sample; between polls the renderer extrapolates.
          double age =
              std::chrono::duration<double>(winrt::clock::now() - timeline.LastUpdatedTime())
                  .count();
          if (next.playing && age >= 0 && age < 86400)
            next.position += age * next.rate;
          next.position = std::clamp(next.position, 0., next.duration);
          next.sampled = Clock::now();
        } else if (!previousKey.empty()) {
          previousKey.clear();
          cover.reset();
          properties = nullptr;
          ++revision;
        }
        next.revision = revision;
        {
          std::lock_guard lock(mutex);
          state = next;
        }
      } catch (...) {
        std::lock_guard lock(mutex);
        state.playing = false;
        state.artist = L"媒体信息暂时不可用";
      }
      for (int i = 0; i < 5 && !stop.stop_requested(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  } catch (...) {
    std::lock_guard lock(mutex);
    state.artist = L"系统媒体接口不可用";
  }
}
}  // namespace jonsbo
