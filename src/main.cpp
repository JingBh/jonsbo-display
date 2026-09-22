// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "app/application.hpp"
#include "app/desktop_panel.hpp"
#include "core/dashboard_state.hpp"
#include "integrations/home_assistant.hpp"
int wmain(int argc, wchar_t** argv) {
  using namespace jonsbo;
  try {
    Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 3 && std::wstring(argv[1]) == L"--ha-token-stdin") {
      std::string token;
      std::getline(std::cin, token);
      bool ok = HomeAssistant::SaveToken(argv[2], token);
      SecureZeroMemory(token.data(), token.size());
      std::cout << (ok ? "Credential stored\n" : "Credential storage failed\n");
      return ok ? 0 : 1;
    }
    ApplicationOptions o;
    o.config = ExeDir() / L"jonsbo-display.ini";
    bool select = false;
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    o.reduced = !animations;
    for (int i = 1; i < argc; ++i) {
      std::wstring arg = argv[i];
      if (arg == L"--usb")
        o.usb = true;
      else if (arg == L"--no-usb")
        o.usb = false;
      else if (arg == L"--check-integrations")
        o.checkIntegrations = true;
      else if (arg == L"--reduced-motion")
        o.reduced = true;
      else if (arg == L"--tab" && i + 1 < argc) {
        std::wstring key = argv[++i];
        auto it = std::find(kPageKeys.begin(), kPageKeys.end(), key);
        if (it == kPageKeys.end())
          throw std::runtime_error("Unknown tab");
        o.tab = int(it - kPageKeys.begin());
        select = true;
      } else if (arg == L"--seconds" && i + 1 < argc)
        o.seconds = std::stoi(argv[++i]);
      else if (arg == L"--config" && i + 1 < argc)
        o.config = std::filesystem::absolute(argv[++i]);
      else if (arg == L"--help") {
        std::cout
            << "jonsbo-display [--no-usb] [--tab monitor|home|codex|music|video] [--seconds N] "
               "[--reduced-motion] [--config PATH] [--check-integrations]\n";
        return 0;
      } else
        throw std::runtime_error("Unknown or incomplete argument");
    }
    if (o.checkIntegrations)
      return CheckIntegrations(o.config);
    if (select) {
      HWND existing = FindWindowW(kWindowClass, nullptr);
      if (!existing)
        EnumChildWindows(
            GetDesktopWindow(),
            [](HWND h, LPARAM p) -> BOOL {
              wchar_t cls[64]{};
              GetClassNameW(h, cls, 64);
              if (std::wstring(cls) == kWindowClass) {
                *reinterpret_cast<HWND*>(p) = h;
                return FALSE;
              }
              return TRUE;
            },
            reinterpret_cast<LPARAM>(&existing));
      if (existing) {
        COPYDATASTRUCT d{480, sizeof(o.tab), &o.tab};
        SendMessageW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&d));
        return 0;
      }
    }
    if (!select) {
      auto saved = Ini(o.config, L"display", L"last_tab");
      auto it = std::find(kPageKeys.begin(), kPageKeys.end(), saved);
      if (it != kPageKeys.end())
        o.tab = int(it - kPageKeys.begin());
    }
    return RunApplication(std::move(o));
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
