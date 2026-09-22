// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "device/display_session.hpp"
namespace jonsbo {
MSDisplayEdid MakeJonsboDefaultEdid() {
  MSDisplayEdid edid{};
  edid.mode = 2;
  constexpr std::array<MSDisplayTiming, 8> kTimings = {{
      {143, 7, 600, 490, 480, 480, 1764, 6000, 100, 8, 50, 4},
      {147, 7, 452, 568, 240, 320, 1540, 6000, 190, 206, 5, 20},
      {148, 7, 568, 452, 320, 240, 1540, 6000, 206, 190, 20, 5},
      {156, 7, 640, 352, 240, 240, 1350, 6000, 190, 51, 10, 10},
      {159, 7, 600, 375, 480, 272, 1350, 6000, 100, 90, 50, 5},
      {160, 7, 472, 996, 360, 960, 2820, 6000, 88, 33, 32, 10},
      {171, 7, 488, 996, 376, 960, 2916, 6000, 312, 66, 112, 10},
      {177, 7, 672, 1684, 600, 1600, 5658, 5000, 32, 44, 2, 4},
  }};
  edid.append_count = static_cast<int32_t>(kTimings.size());
  std::copy(kTimings.begin(), kTimings.end(), std::begin(edid.timing_append));
  return edid;
}

struct AttachedDisplay {
  std::mutex mutex;
  std::condition_variable changed;
  bool attached = false;
  uint32_t handle = 0;
  std::vector<MSDisplayResolution> resolutions;
};

AttachedDisplay g_attached_display;

void WINAPI OnAttach(uint32_t handle, const MSDisplayResolution* resolutions, int count) {
  std::lock_guard lock(g_attached_display.mutex);
  g_attached_display.handle = handle;
  g_attached_display.attached = true;
  g_attached_display.resolutions.clear();
  if (resolutions && count > 0) {
    const int safe_count = std::min(count, 16);
    g_attached_display.resolutions.assign(resolutions, resolutions + safe_count);
  }
  g_attached_display.changed.notify_all();
}

void WINAPI OnDetach(uint32_t handle) {
  std::lock_guard lock(g_attached_display.mutex);
  if (g_attached_display.handle == handle)
    g_attached_display.attached = false;
  g_attached_display.changed.notify_all();
}

std::vector<uint8_t> ResampleFrame(const Frame& frame, uint32_t target_width,
                                   uint32_t target_height) {
  std::vector<uint8_t> output(static_cast<size_t>(target_width) * target_height * 4);
  for (uint32_t y = 0; y < target_height; ++y) {
    const uint32_t source_y = y * kScreenHeight / target_height;
    for (uint32_t x = 0; x < target_width; ++x) {
      const uint32_t source_x = x * kScreenWidth / target_width;
      const auto source = (static_cast<size_t>(source_y) * kScreenWidth + source_x) * 4;
      const auto destination = (static_cast<size_t>(y) * target_width + x) * 4;
      std::memcpy(output.data() + destination, frame.data() + source, 4);
    }
  }
  return output;
}

// The recovered SDK accepts 32-bit BGRA, but the device visibly quantizes it
// to 5:6:5.  Distribute each quantization boundary with deterministic, spatial
// noise.  Unlike a small ordered matrix, this avoids creating visible stripe
// patterns over a smooth one-directional gradient; it remains static frame to
// frame, so a dashboard does not shimmer.
void ApplyRgb565Dither(std::vector<uint8_t>* pixels, uint32_t width, uint32_t height) {
  const auto quantize = [](uint8_t value, int bits, int threshold) -> uint8_t {
    const int levels = (1 << bits) - 1;
    const int scaled = static_cast<int>(value) * levels;
    int level = scaled / 255;
    const int remainder = scaled % 255;
    if (remainder * 32 > (threshold * 2 + 1) * 255 && level < levels)
      ++level;
    return static_cast<uint8_t>((level * 255 + levels / 2) / levels);
  };

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t index = (static_cast<size_t>(y) * width + x) * 4;
      uint32_t noise = x * 0x1F123BB5u + y * 0x5F356495u;
      noise ^= noise >> 16;
      noise *= 0x45D9F3Bu;
      noise ^= noise >> 16;
      const int threshold = static_cast<int>(noise >> 28);                  // [0, 15]
      (*pixels)[index + 0] = quantize((*pixels)[index + 0], 5, threshold);  // B
      (*pixels)[index + 1] = quantize((*pixels)[index + 1], 6, threshold);  // G
      (*pixels)[index + 2] = quantize((*pixels)[index + 2], 5, threshold);  // R
    }
  }
}

bool JonsboDisplaySession::Open(int mode_index, std::wstring* error) {
  wchar_t local[32768]{};
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) {
    *error = L"LOCALAPPDATA is unavailable.";
    return false;
  }
  const auto sdk_path =
      std::filesystem::path(local) / L"JONSBO-AIO/dll/x64/MSDISPLAYSDKWRRAPER.dll";
  module_ = LoadLibraryExW(sdk_path.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!module_) {
    *error = L"Cannot load the Jonsbo SDK: " + sdk_path.wstring();
    return false;
  }
  start_ = reinterpret_cast<StartFn>(GetProcAddress(module_, "Wrraper_MSDisplayStart"));
  stop_ = reinterpret_cast<StopFn>(GetProcAddress(module_, "Wrraper_MSDisplayStop"));
  register_callback_ = reinterpret_cast<RegisterCallbackFn>(
      GetProcAddress(module_, "Wrraper_MSDisplayRegisterCallback"));
  set_video_param_ =
      reinterpret_cast<SetVideoParamFn>(GetProcAddress(module_, "Wrraper_MSDisplaySetVideoParam"));
  send_picture_ =
      reinterpret_cast<SendPictureFn>(GetProcAddress(module_, "Wrraper_MSDisplaySendPicture"));
  if (!start_ || !stop_ || !register_callback_ || !set_video_param_ || !send_picture_) {
    *error = L"The installed Jonsbo SDK does not expose its normal display API.";
    Close();
    return false;
  }

  {
    std::lock_guard lock(g_attached_display.mutex);
    g_attached_display.attached = false;
    g_attached_display.handle = 0;
    g_attached_display.resolutions.clear();
  }
  register_callback_(&OnAttach, &OnDetach);
  edid_ = MakeJonsboDefaultEdid();
  const int start_result = start_(1, &edid_);
  if (start_result != 0) {
    *error = L"Jonsbo SDK start failed with code " + std::to_wstring(start_result) + L".";
    Close();
    return false;
  }
  started_ = true;

  std::vector<MSDisplayResolution> resolutions;
  {
    std::unique_lock lock(g_attached_display.mutex);
    const bool attached = g_attached_display.changed.wait_for(
        lock, std::chrono::seconds(5), [] { return g_attached_display.attached; });
    if (attached) {
      handle_ = g_attached_display.handle;
      resolutions = g_attached_display.resolutions;
    }
  }
  if (handle_ == 0 || resolutions.empty()) {
    *error = L"No Jonsbo display with usable video modes attached within five seconds.";
    Close();
    return false;
  }

  std::wcout << L"Jonsbo display attached; supported modes:";
  for (const auto& resolution : resolutions) {
    std::wcout << L" " << resolution.width << L"x" << resolution.height << L"@"
               << resolution.refresh;
  }
  std::wcout << L"\n";

  size_t selected_mode_index = 0;
  if (mode_index >= 0) {
    selected_mode_index = static_cast<size_t>(mode_index);
  } else {
    const auto panel_mode =
        std::find_if(resolutions.begin(), resolutions.end(), [](const MSDisplayResolution& item) {
          return item.width == kScreenWidth && item.height == kScreenHeight;
        });
    if (panel_mode != resolutions.end()) {
      selected_mode_index = static_cast<size_t>(std::distance(resolutions.begin(), panel_mode));
    }
  }
  if (selected_mode_index >= resolutions.size()) {
    *error = L"--mode-index is outside the device-reported mode list.";
    Close();
    return false;
  }
  mode_ = resolutions[selected_mode_index];
  if (mode_.width == 0 || mode_.height == 0 || mode_.width > 4096 || mode_.height > 4096) {
    *error = L"The selected device video mode is not safe to use.";
    Close();
    return false;
  }
  std::wcout << L"Using device mode " << selected_mode_index << L": " << mode_.width << L"x"
             << mode_.height << L"@" << mode_.refresh << L"\n";

  const int mode_result = set_video_param_(handle_, &mode_);
  if (mode_result != 0) {
    *error = L"Jonsbo video mode setup failed with code " + std::to_wstring(mode_result) + L".";
    Close();
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  transport_pixels_.resize(static_cast<size_t>(mode_.width) * mode_.height * 4);
  unmanaged_pixels_ = static_cast<uint8_t*>(LocalAlloc(LMEM_FIXED, transport_pixels_.size()));
  unmanaged_picture_ =
      static_cast<MSDisplayPicture*>(LocalAlloc(LMEM_FIXED, sizeof(MSDisplayPicture)));
  if (!unmanaged_pixels_ || !unmanaged_picture_) {
    *error = L"Cannot allocate the SDK image buffers.";
    Close();
    return false;
  }
  *unmanaged_picture_ = {mode_.width, mode_.height, unmanaged_pixels_};
  return true;
}

bool JonsboDisplaySession::Send(const Frame& frame, bool dither_rgb565, int* result,
                                std::wstring* error) {
  if (!unmanaged_pixels_ || !unmanaged_picture_ || !send_picture_) {
    *error = L"Jonsbo display session is not initialized.";
    return false;
  }
  transport_pixels_ = ResampleFrame(frame, mode_.width, mode_.height);
  if (dither_rgb565)
    ApplyRgb565Dither(&transport_pixels_, mode_.width, mode_.height);
  std::memcpy(unmanaged_pixels_, transport_pixels_.data(), transport_pixels_.size());
  *result = send_picture_(handle_, unmanaged_picture_, false);
  return true;
}

int JonsboDisplaySession::RepeatPreparedFrame() {
  return send_picture_ ? send_picture_(handle_, unmanaged_picture_, false) : -1;
}

void JonsboDisplaySession::Close() {
  if (started_ && stop_)
    stop_();
  started_ = false;
  if (unmanaged_picture_)
    LocalFree(unmanaged_picture_);
  if (unmanaged_pixels_)
    LocalFree(unmanaged_pixels_);
  unmanaged_picture_ = nullptr;
  unmanaged_pixels_ = nullptr;
  if (module_)
    FreeLibrary(module_);
  module_ = nullptr;
}
}  // namespace jonsbo
