// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/utilities.hpp"
#include "device/frame.hpp"

namespace jonsbo {

// These layouts are reproduced from the installed Jonsbo application's managed
// metadata. They only cover normal display streaming, never Flash/EEPROM APIs.
struct MSDisplayPicture {
  uint32_t width;
  uint32_t height;
  void* data;
};

struct MSDisplayResolution {
  uint32_t width;
  uint32_t height;
  uint32_t refresh;
};

struct MSDisplayTiming {
  uint32_t vic;
  uint32_t polarity;
  uint32_t htotal;
  uint32_t vtotal;
  uint32_t hactive;
  uint32_t vactive;
  uint32_t pixclk;
  uint32_t vfreq;
  uint32_t hoffset;
  uint32_t voffset;
  uint32_t hsyncwidth;
  uint32_t vsyncwidth;
};

struct MSDisplayEdid {
  int32_t mode;
  char edid_replace[128];
  int32_t append_count;
  MSDisplayTiming timing_append[16];
};

static_assert(sizeof(MSDisplayPicture) == 16);
static_assert(sizeof(MSDisplayResolution) == 12);
static_assert(sizeof(MSDisplayTiming) == 48);
static_assert(sizeof(MSDisplayEdid) == 904);

class JonsboDisplaySession {
 public:
  ~JonsboDisplaySession() {
    Close();
  }

  bool Open(int mode_index, std::wstring* error);

  bool Send(const Frame& frame, bool dither_rgb565, int* result, std::wstring* error);

  int RepeatPreparedFrame();

 private:
  using StartFn = int(WINAPI*)(int, const MSDisplayEdid*);
  using StopFn = int(WINAPI*)();
  using AttachCallback = void(WINAPI*)(uint32_t, const MSDisplayResolution*, int);
  using DetachCallback = void(WINAPI*)(uint32_t);
  using RegisterCallbackFn = void(WINAPI*)(AttachCallback, DetachCallback);
  using SetVideoParamFn = int(WINAPI*)(uint32_t, const MSDisplayResolution*);
  using SendPictureFn = int(WINAPI*)(uint32_t, const MSDisplayPicture*, bool);

  void Close();

  HMODULE module_ = nullptr;
  StartFn start_ = nullptr;
  StopFn stop_ = nullptr;
  RegisterCallbackFn register_callback_ = nullptr;
  SetVideoParamFn set_video_param_ = nullptr;
  SendPictureFn send_picture_ = nullptr;
  bool started_ = false;
  uint32_t handle_ = 0;
  MSDisplayEdid edid_{};
  MSDisplayResolution mode_{};
  std::vector<uint8_t> transport_pixels_;
  uint8_t* unmanaged_pixels_ = nullptr;
  MSDisplayPicture* unmanaged_picture_ = nullptr;
};
}  // namespace jonsbo
