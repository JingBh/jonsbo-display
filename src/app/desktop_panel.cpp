// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "app/desktop_panel.hpp"
namespace jonsbo {
// The panel remains a PMv2 top-level tool window. Never SetParent into Explorer:
// cross-process parenting may reset the process DPI awareness.
int PanelHit(float x, float y) {
  if (x < 12 || x >= 468 || y < 10 || y >= 78)
    return -1;
  int index = int((x - 12) / 76);
  return x - (12 + index * 76) < 72 ? index : -1;
}
void PanelPoint(HWND w, LPARAM lp, float& x, float& y) {
  RECT r{};
  GetClientRect(w, &r);
  x = short(LOWORD(lp)) * 480.f / std::max(1L, r.right);
  y = short(HIWORD(lp)) * 88.f / std::max(1L, r.bottom);
}
void PlacePanel(HWND w) {
  RECT r{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
  UINT dpi = GetDpiForWindow(w);
  int width = MulDiv(480, dpi, 96), height = MulDiv(88, dpi, 96), gap = MulDiv(16, dpi, 96);
  SetWindowPos(w, nullptr, r.right - width - gap, r.bottom - height - gap, width, height,
               SWP_NOZORDER | SWP_NOACTIVATE);
}
void MaintainDesktopLayer(HWND panel) {
  struct Search {
    HWND self, previous = nullptr, after = nullptr;
    bool found = false;
  } search{panel};
  EnumWindows(
      [](HWND w, LPARAM param) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(param);
        if (w == s.self || !IsWindowVisible(w))
          return TRUE;
        wchar_t cls[256]{};
        GetClassNameW(w, cls, 256);
        std::wstring name = cls;
        bool desktop = name == L"Progman" || name == L"WorkerW" ||
                       name.rfind(L"HwndWrapper[Aura Wallpaper Service.exe;", 0) == 0;
        if (desktop) {
          s.after = s.previous;
          s.found = true;
          return FALSE;
        }
        s.previous = w;
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  // Insert immediately above wallpaper/desktop, below ordinary application windows.
  if (search.found) {
    HWND after = search.after ? search.after : HWND_TOP;
    if (GetWindow(panel, GW_HWNDPREV) != search.after || !IsWindowVisible(panel))
      SetWindowPos(panel, after, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
}

}  // namespace jonsbo
