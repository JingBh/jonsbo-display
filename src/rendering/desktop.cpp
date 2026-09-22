// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "rendering/screen_renderer.hpp"

namespace jonsbo {
void ScreenRenderer::CreatePanelTarget() {
  ComPtr<IDXGISurface> back;
  Check(swap->GetBuffer(0, IID_PPV_ARGS(&back)));
  auto prop = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  Check(ctx->CreateBitmapFromDxgiSurface(back.Get(), &prop, &windowTarget));
}

void ScreenRenderer::Present(int active, int hover, bool force) {
  if (swap) {
    RECT r{};
    GetClientRect(hwnd, &r);
    UINT width = std::max(1L, r.right), height = std::max(1L, r.bottom);
    if (width != panelWidth || height != panelHeight) {
      ctx->SetTarget(nullptr);
      windowTarget.Reset();
      Check(swap->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
      panelWidth = width;
      panelHeight = height;
      CreatePanelTarget();
      force = true;
    }
  }
  if (!swap || (!force && active == controlActive && hover == controlHover))
    return;
  controlActive = active;
  controlHover = hover;
  ctx->SetDpi(96.f * panelWidth / 480, 96.f * panelHeight / 88);
  ctx->SetTarget(windowTarget.Get());
  ctx->BeginDraw();
  ctx->Clear(Color(0, 0));
  for (int i = 0; i < 6; ++i) {
    float x = 12 + i * 76.f;
    bool selected = i < 5 && i == active;
    auto r = D2D1::RectF(x, 10, x + 72, 78);
    Round(x, 10, 72, 68, 16, selected ? 0x55516f : 0x1b1d2b,
          selected     ? .62f
          : i == hover ? .52f
                       : .34f);
    if (selected)
      ClipRound(r, 16, [&] {
        Gradient(
            r,
            {{0, Color(0xffffff, .15f)}, {.6f, Color(0xffffff, .015f)}, {1, Color(0xb4a6d4, .06f)}},
            {x, 10}, {x + 72, 78});
      });
    brush->SetColor(Color(0xffffff, selected ? .17f : .04f));
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(r, 16, 16), brush.Get(), 1);
    const wchar_t* navigation[] = {
        L"monitor", L"house", L"code-xml", L"music-2", L"picture-in-picture-2", L"power"};
    Icon(navigation[i], x + 22, 17, 28, selected ? 0xf5f5fa : 0xa6a3b2);
    Text(i == 5 ? L"退出" : kPageNames[i], x + 36, 65, 12, selected ? 0xf5f5fa : 0xa6a3b2, 400, 1);
  }
  Check(ctx->EndDraw());
  ctx->SetDpi(96, 96);
  Check(swap->Present(0, 0));
}
}  // namespace jonsbo
