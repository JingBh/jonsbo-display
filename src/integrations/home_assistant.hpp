// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/utilities.hpp"
#include <winhttp.h>
#include <wincred.h>
#include <winrt/Windows.Data.Json.h>

namespace jonsbo {
struct HomeState {
  struct Device {
    std::wstring value = L"—";
    bool active = false, available = false;
    unsigned color = 0;
  };
  std::wstring temperature = L"—", humidity = L"—", status = L"未连接";
  std::array<Device, 3> lights;
  std::array<Device, 2> covers, climate;
  bool connected = false;
};
std::wstring CurtainLabel(double position);
double ColorLuminance(unsigned color);
unsigned ReadableLightColor(unsigned color);
unsigned LightColor(const winrt::Windows::Data::Json::JsonObject& object);
class HomeAssistant {
 public:
  HomeAssistant(const std::filesystem::path& config, bool enabled) : config(config) {
    if (enabled)
      worker = std::jthread([this](std::stop_token stop) { Run(stop); });
  }
  HomeState Read();
  static std::wstring CredentialTarget(const std::wstring& host);
  static bool SaveToken(const std::wstring& host, std::string token);

 private:
  using Json = winrt::Windows::Data::Json::JsonObject;
  struct Internet {
    HINTERNET handle = nullptr;
    explicit Internet(HINTERNET h) : handle(h) {}
    ~Internet() {
      if (handle)
        WinHttpCloseHandle(handle);
    }
    operator HINTERNET() const {
      return handle;
    }
  };
  static double Number(const Json& o, const wchar_t* key, double fallback = -1);
  std::string Request(const std::wstring& host, INTERNET_PORT port, const std::wstring& path,
                      const std::string& body);
  void Run(std::stop_token stop);
  std::filesystem::path config;
  std::mutex mutex;
  HomeState state;
  std::jthread worker;
};
}  // namespace jonsbo
