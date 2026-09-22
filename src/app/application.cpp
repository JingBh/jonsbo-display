// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "app/application.hpp"
#include "app/desktop_panel.hpp"
#include "core/dashboard_state.hpp"
#include "device/display_transport.hpp"
#include "integrations/home_assistant.hpp"
#include "integrations/system_media.hpp"
#include "integrations/system_telemetry.hpp"
#include "rendering/screen_renderer.hpp"
#include <dwmapi.h>

namespace jonsbo {
class Application {
 public:
  explicit Application(ApplicationOptions o)
      : options(std::move(o)),
        active(options.tab),
        marker(float(active)),
        telemetry(options.config),
        systemMedia(true),
        homeAssistant(options.config, true) {}
  void Switch(int tab) {
    if (tab < 0 || tab > 4 || tab == active || !renderer)
      return;
    renderer->Snapshot();
    startMarker = marker;
    direction = tab > marker ? 1.f : -1.f;
    active = tab;
    renderer->DrawPage(active, telemetry.Read());
    transitionStart = Clock::now();
    animating = !options.reduced;
    dirty = true;
    if (!animating)
      marker = float(tab);
  }
  static LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* a = reinterpret_cast<Application*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      a = static_cast<Application*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
      SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
    }
    if (!a)
      return DefWindowProcW(w, msg, wp, lp);
    if (msg == WM_COMMAND && LOWORD(wp) >= 100 && LOWORD(wp) < 105) {
      a->Switch(LOWORD(wp) - 100);
      return 0;
    }
    if (msg == WM_LBUTTONUP) {
      float x, y;
      PanelPoint(w, lp, x, y);
      int tab = PanelHit(x, y);
      if (tab == 5)
        SendMessageW(w, WM_CLOSE, 0, 0);
      else if (tab >= 0)
        a->Switch(tab);
      return 0;
    }
    if (msg == WM_MOUSEMOVE) {
      float x, y;
      PanelPoint(w, lp, x, y);
      int hover = PanelHit(x, y);
      if (hover != a->hover) {
        a->hover = hover;
        a->controlDirty = true;
      }
      TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, w, 0};
      TrackMouseEvent(&tracking);
      return 0;
    }
    if (msg == WM_MOUSELEAVE) {
      a->hover = -1;
      a->controlDirty = true;
      return 0;
    }
    if (msg == WM_KEYDOWN && wp >= '1' && wp <= '5') {
      a->Switch(int(wp - '1'));
      return 0;
    }
    if (msg == WM_COPYDATA) {
      auto* d = reinterpret_cast<COPYDATASTRUCT*>(lp);
      if (d->dwData == 480 && d->cbData == sizeof(int)) {
        int index;
        std::memcpy(&index, d->lpData, sizeof(index));
        a->Switch(index);
        return TRUE;
      }
    }
    if (msg == WM_PAINT) {
      PAINTSTRUCT p;
      BeginPaint(w, &p);
      EndPaint(w, &p);
      a->controlDirty = true;
      return 0;
    }
    if (msg == WM_ERASEBKGND)
      return 1;
    if (msg == WM_MOUSEACTIVATE)
      return MA_NOACTIVATE;
    if (msg == WM_DPICHANGED) {
      auto r = reinterpret_cast<RECT*>(lp);
      SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      a->controlDirty = true;
      return 0;
    }
    if (msg == WM_DISPLAYCHANGE) {
      PlacePanel(w);
      a->controlDirty = true;
      return 0;
    }
    if (msg == WM_POWERBROADCAST && (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND)) {
      if (a->options.usb && Seconds(a->lastUsbRestart) > 2) {
        a->lastUsbRestart = Clock::now();
        a->transport.Restart();
        a->dirty = true;
      }
      return TRUE;
    }
    if (msg == WM_CLOSE) {
      if (MessageBoxW(w, L"确定退出小屏幕程序？退出后将停止向屏幕输出。", L"退出小屏幕",
                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND) ==
          IDYES)
        DestroyWindow(w);
      return 0;
    }
    if (msg == WM_DESTROY) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
  }
  int Run() {
    {
      WNDCLASSW wc{};
      wc.lpfnWndProc = Proc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = kWindowClass;
      wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
      RegisterClassW(&wc);
      // Keep an independent DPI context; no cross-process Explorer parenting.
      RECT area{};
      SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
      window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                               kWindowClass, L"小屏幕控制", WS_POPUP, area.right - 504,
                               area.bottom - 112, 480, 88, nullptr, nullptr, wc.hInstance, this);
      if (!window)
        throw std::runtime_error("Cannot create control window");
      PlacePanel(window);
      BOOL dark = TRUE;
      DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }
    renderer = std::make_unique<ScreenRenderer>(window);
    renderer->SetReducedMotion(options.reduced);
    DashboardState state = telemetry.Read();
    auto media = systemMedia.Read();
    renderer->SetMedia(media);
    renderer->SetHome(homeAssistant.Read());
    auto started = Clock::now();
    for (int i = 0; i < 5; ++i)
      renderer->DrawPage(i, state);
    renderer->Compose(active, marker, false, 1, 0, 0);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    MaintainDesktopLayer(window);
    renderer->Present(active, hover, true);
    std::cout << "Control ready\n" << std::flush;
    if (options.usb) {
      transport.Start();
      renderer->QueueReadback();
    }
    auto nextSample = Clock::now() + std::chrono::seconds(1), nextMedia = Clock::now(),
         nextPip = Clock::now();
    Frame frame;
    bool done = false;
    int beforePip = active;
    std::wstring lastError;
    SYSTEMTIME clock{};
    GetLocalTime(&clock);
    auto nextDesktop = Clock::now();
    while (!done) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          done = true;
          break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam >= '1' && msg.wParam <= '5') {
          Switch(int(msg.wParam - '1'));
          continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (done)
        break;
      auto now = Clock::now();
      if (options.seconds > 0 && Seconds(started) >= options.seconds)
        break;
      if (now >= nextDesktop) {
        MaintainDesktopLayer(window);
        nextDesktop = now + std::chrono::seconds(1);
      }
      if (now >= nextPip) {
        int change = renderer->DetectPictureInPicture();
        if (change > 0) {
          std::cout << "PiP capture started\n" << std::flush;
          beforePip = active;
          Switch(4);
        } else if (change < 0 && active == 4) {
          std::cout << "PiP capture ended\n" << std::flush;
          Switch(beforePip == 4 ? 3 : beforePip);
        }
        nextPip = now + std::chrono::milliseconds(500);
      }
      if (now >= nextSample) {
        state = telemetry.Read();
        renderer->SetHome(homeAssistant.Read());
        if (active <= 2) {
          renderer->DrawPage(active, state);
          dirty = true;
        }
        GetLocalTime(&clock);
        dirty = true;
        nextSample = now + std::chrono::seconds(1);
      }
      if (now >= nextMedia) {
        auto latest = systemMedia.Read();
        bool changed = latest.revision != media.revision || latest.title != media.title ||
                       latest.artist != media.artist || latest.playing != media.playing;
        media = latest;
        renderer->SetMedia(media);
        if (active == 3 && changed) {
          renderer->DrawPage(3, state);
          dirty = true;
        }
        nextMedia = now + std::chrono::milliseconds(250);
      }
      bool quota = active == 2 && renderer->QuotaAnimating();
      if (quota) {
        renderer->DrawPage(2, state);
        dirty = true;
      }
      bool music = active == 3 && media.playing && (options.usb || !IsIconic(window));
      if (music) {
        renderer->UpdateMusicProgress();
        dirty = true;
      }
      bool video =
          active == 4 && renderer->HasPictureInPicture() && (options.usb || !IsIconic(window));
      bool videoChanged = video && renderer->UpdatePictureInPicture();
      if (videoChanged)
        renderer->DrawPage(4, state);
      if (dirty || animating || videoChanged) {
        float t = animating ? std::min(1.f, Seconds(transitionStart) / .46f) : 1;
        float spring = t == 1 ? 1 : 1 - (1 + 10 * t) * std::exp(-10 * t);
        marker = animating ? startMarker + (active - startMarker) * spring : float(active);
        renderer->Compose(active, marker, animating, t, direction,
                          animating ? 6 * std::sin(3.14159265f * t) : 0);
        if (options.usb)
          renderer->QueueReadback();
        if (t >= 1)
          animating = false;
        dirty = false;
      }
      renderer->Present(active, hover, controlDirty);
      controlDirty = false;
      if (options.usb && renderer->ReadLatest(frame))
        transport.Submit(frame);
      auto error = transport.Status();
      if (!error.empty() && error != lastError) {
        lastError = error;
        std::wcerr << error << L"\n";
        SetWindowTextW(window, (L"Jonsbo Display — USB: " + error).c_str());
      }
      MsgWaitForMultipleObjectsEx(0, nullptr,
                                  (animating || quota || video || music || options.usb) ? 16 : 100,
                                  QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    if (options.seconds == 0) {
      if (!WritePrivateProfileStringW(L"display", L"last_tab", kPageKeys[active].c_str(),
                                      options.config.c_str()))
        std::cerr << "Could not save last page\n";
    }
    std::cout << "Rendered frames: " << renderer->rendered
              << "; USB accepted: " << transport.accepted << "; USB other: " << transport.rejected
              << "\n";
    return options.usb && (!transport.Status().empty() || transport.accepted == 0) ? 2 : 0;
  }

 private:
  ApplicationOptions options;
  int active, hover = -1;
  float marker, startMarker = 0, direction = 0;
  bool animating = false, dirty = true, controlDirty = true;
  Clock::time_point transitionStart;
  Clock::time_point lastUsbRestart;
  HWND window = nullptr;
  SystemTelemetry telemetry;
  SystemMedia systemMedia;
  HomeAssistant homeAssistant;
  DisplayTransport transport;
  std::unique_ptr<ScreenRenderer> renderer;
};

int RunApplication(ApplicationOptions options) {
  Application app(std::move(options));
  return app.Run();
}
}  // namespace jonsbo
