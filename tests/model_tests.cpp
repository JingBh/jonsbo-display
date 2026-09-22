// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "app/desktop_panel.hpp"
#include "core/utilities.hpp"
#include "device/numeric_panel.hpp"
#include "integrations/codex_client.hpp"
#include "integrations/home_assistant.hpp"
#include "rendering/media_playhead.hpp"
#include "rendering/quota_animation.hpp"
namespace jonsbo {
int ModelTests() {
  for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
    for (int i = 0; i < 6; ++i) {
      float physicalX = (48 + i * 76) * scale, physicalY = 44 * scale;
      if (PanelHit(physicalX / scale, physicalY / scale) != i)
        throw std::runtime_error("DPI button hit");
    }
    if (PanelHit(86, 44) != -1 || PanelHit(48, 4) != -1)
      throw std::runtime_error("panel gap hit");
  }
  for (unsigned color : {0u, 0x0000ffu, 0xff0000u, 0x001122u, 0xffcea6u, 0xffffffu}) {
    unsigned lifted = ReadableLightColor(color);
    if ((ColorLuminance(lifted) + .05) / (ColorLuminance(0x41415b) + .05) < 4.5)
      throw std::runtime_error("light contrast");
  }
  if (ReadableLightColor(0xffffff) != 0xffffff)
    throw std::runtime_error("bright color changed");
  auto require = [](bool test, const char* message) {
    if (!test)
      throw std::runtime_error(message);
  };
  using Json = winrt::Windows::Data::Json::JsonObject;
  auto parse = [](const wchar_t* text) { return CodexLimits::Parse(Json::Parse(text)); };
  auto two = parse(
      LR"({"rateLimits":{"primary":{"usedPercent":32,"windowDurationMins":300,"resetsAt":123},"secondary":{"usedPercent":68,"windowDurationMins":10080,"resetsAt":456}}})");
  require(
      two.windows.size() == 2 && 100 - two.windows[0].used == 68 && 100 - two.windows[1].used == 32,
      "remaining conversion");
  auto weekly = parse(
      LR"({"rateLimits":{"primary":null,"secondary":{"usedPercent":0,"windowDurationMins":10080,"resetsAt":456}}})");
  require(weekly.windows.size() == 1 && weekly.windows[0].minutes == 10080, "weekly only");
  auto monthly = parse(
      LR"({"rateLimitsByLimitId":{"codex":{"primary":{"usedPercent":100,"windowDurationMins":43200,"resetsAt":456},"secondary":null}}})");
  require(monthly.windows.size() == 1 && 100 - monthly.windows[0].used == 0,
          "monthly only / zero remaining");
  require(parse(LR"({"rateLimits":null,"rateLimitsByLimitId":null})").windows.empty(),
          "null limits");
  require(parse(LR"({"rateLimits":{"primary":{"usedPercent":null,"windowDurationMins":300}}})")
              .windows.empty(),
          "unknown is not zero");
  MediaPlayhead head;
  head.Set(100, 600, true, 1, 0, true);
  require(std::abs(head.At(1) - 101) < 1e-6, "normal playback");
  head.Set(200, 600, true, 1, 1, false);
  require(std::abs(head.At(1) - 101) < 1e-6, "seek starts continuously");
  require(head.At(1.175) > 101 && head.At(1.175) < 200.175, "seek interpolates");
  require(std::abs(head.At(1.35) - 200.35) < 1e-6, "seek reaches moving target");
  double visible = head.At(1.4);
  head.Set(20, 600, true, 1, 1.4, false);
  require(std::abs(head.At(1.4) - visible) < 1e-6, "reverse seek continuous");
  visible = head.At(1.5);
  head.Set(400, 600, true, 1, 1.5, false);
  require(std::abs(head.At(1.5) - visible) < 1e-6, "rapid seek continuous");
  head.Set(0, 0, false, 1, 2, true);
  require(head.At(3) == 0, "empty media");
  QuotaMotion quota;
  quota.Set(80, 0);
  quota.Set(40, 1);
  require(quota.At(1) == 80, "quota continuity");
  require(quota.At(1.3) < 80 && quota.At(1.3) > 40, "quota interpolation");
  require(std::abs(quota.At(1.65) - 40) < 1e-6, "quota final");
  double midway = quota.At(1.1);
  quota.Set(90, 1.1);
  require(std::abs(quota.At(1.1) - midway) < 1e-6, "quota interruption");
  quota.Set(60, 2, true);
  require(quota.At(2) == 60 && !quota.Active(2), "reduced motion quota");
  require(
      BitRate(100) == L"800 bps" && BitRate(125) == L"1.0 Kbps" && BitRate(125000) == L"1.0 Mbps",
      "adaptive bit rate units");
  require(CurtainLabel(0) == L"关" && CurtainLabel(10) == L"关" && CurtainLabel(10.1) == L"半关" &&
              CurtainLabel(49.9) == L"半关" && CurtainLabel(50) == L"半开" &&
              CurtainLabel(89.9) == L"半开" && CurtainLabel(90) == L"开" &&
              CurtainLabel(100) == L"开",
          "curtain thresholds");
  require(LightColor(Json::Parse(LR"({"rgb_color":[255,206,166]})")) == 0xffcea6, "HA RGB color");
  require(LightColor(Json::Parse(LR"({"rgb_color":null,"color_temp_kelvin":4000})")) == 0xffcea6,
          "HA temperature color");
  require(LightColor(Json::Parse(LR"({"rgb_color":[0,0,0]})")) == 0, "black RGB is valid");
  require(LightColor(Json::Parse(LR"({"rgb_color":[1],"color_temp_kelvin":null})")) == 0xe6cfae,
          "invalid color fallback");
  SYSTEMTIME time{};
  time.wYear = 2026;
  time.wMonth = 9;
  time.wDay = 22;
  time.wHour = 23;
  time.wMinute = 7;
  time.wSecond = 5;
  time.wDayOfWeek = 2;
  auto numeric = EncodeNumericPanelReport({37.6f, 64.375f, 49.5f, 5545.f}, time);
  require(numeric[0] == 0 && numeric[1] == 1 && numeric[2] == 2, "numeric panel header");
  require(numeric[3] == 64 && numeric[4] == 38 && numeric[5] == 0 && numeric[6] == 38,
          "numeric panel CPU values");
  require(numeric[9] == 55 && numeric[10] == 45, "numeric panel CPU frequency");
  require(numeric[13] == 49 && numeric[14] == 50 && numeric[15] == 0,
          "numeric panel GPU temperature");
  require(numeric[25] == 20 && numeric[26] == 26 && numeric[27] == 9 && numeric[28] == 22 &&
              numeric[29] == 23 && numeric[30] == 7 && numeric[31] == 5 && numeric[32] == 2,
          "numeric panel clock");
  std::cout << "Model tests passed: remaining, weekly-only, monthly-only, smooth seek, quota "
               "transitions, adaptive bit rates, curtain thresholds, light colors\n";
  return 0;
}
}  // namespace jonsbo
int main() {
  try {
    jonsbo::Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    return jonsbo::ModelTests();
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
