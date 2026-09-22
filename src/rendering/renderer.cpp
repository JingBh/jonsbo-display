// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "rendering/screen_renderer.hpp"

namespace jonsbo {
ScreenRenderer::ScreenRenderer(HWND window) : hwnd(window) {
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL level;
  Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                          D3D11_SDK_VERSION, &device, &level, &gpu));
  pip = std::make_unique<PictureInPicture>(device.Get());
  ComPtr<IDXGIDevice> dxgi;
  Check(device.As(&dxgi));
  Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
                          reinterpret_cast<void**>(factory.GetAddressOf())));
  Check(factory->CreateDevice(dxgi.Get(), &d2d));
  Check(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx));
  Check(ctx.As(&svgContext));
  Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(write.GetAddressOf())));
  Check(
      CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
  Check(ctx->CreateSolidColorBrush(Color(0xffffff), &brush));
  ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  ctx->SetDpi(96, 96);
  output = CreateSurface();
  previous = CreateSurface();
  musicBase = CreateSurface();
  mediaAmbient = CreateSurface();
  twilight = CreateSurface();
  for (auto& p : pages)
    p = CreateSurface();
  RenderTwilight();
  if (hwnd) {
    ComPtr<IDXGIAdapter> adapter;
    Check(dxgi->GetAdapter(&adapter));
    ComPtr<IDXGIFactory2> f;
    Check(adapter->GetParent(IID_PPV_ARGS(&f)));
    RECT client{};
    GetClientRect(hwnd, &client);
    panelWidth = std::max(1L, client.right);
    panelHeight = std::max(1L, client.bottom);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = panelWidth;
    desc.Height = panelHeight;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    ComPtr<IDXGISwapChain1> chain;
    Check(f->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &chain));
    Check(chain.As(&swap));
    CreatePanelTarget();
    Check(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition)));
    Check(composition->CreateTargetForHwnd(hwnd, TRUE, &compositionTarget));
    Check(composition->CreateVisual(&panelVisual));
    Check(panelVisual->SetContent(swap.Get()));
    Check(compositionTarget->SetRoot(panelVisual.Get()));
    Check(composition->Commit());
  }
  for (auto& r : readbacks) {
    D3D11_TEXTURE2D_DESC d{};
    output.texture->GetDesc(&d);
    d.BindFlags = 0;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device->CreateTexture2D(&d, nullptr, &r.texture));
    D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0};
    Check(device->CreateQuery(&q, &r.query));
  }
}

Surface ScreenRenderer::CreateSurface() {
  Surface s;
  D3D11_TEXTURE2D_DESC d{};
  d.Width = d.Height = 480;
  d.MipLevels = d.ArraySize = 1;
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  Check(device->CreateTexture2D(&d, nullptr, &s.texture));
  ComPtr<IDXGISurface> surface;
  Check(s.texture.As(&surface));
  auto p = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                                   D2D1::PixelFormat(d.Format, D2D1_ALPHA_MODE_PREMULTIPLIED));
  Check(ctx->CreateBitmapFromDxgiSurface(surface.Get(), &p, &s.bitmap));
  return s;
}

void ScreenRenderer::SetHome(const HomeState& value) {
  home = value;
}

void ScreenRenderer::SetReducedMotion(bool value) {
  reducedMotion = value;
}

bool ScreenRenderer::QuotaAnimating() const {
  for (const auto& [_, q] : quotaMotion)
    if (q.Active(ClockSeconds()))
      return true;
  return false;
}

void ScreenRenderer::Snapshot() {
  ctx->SetTarget(nullptr);
  gpu->CopyResource(previous.texture.Get(), output.texture.Get());
}

void ScreenRenderer::Compose(int tab, float marker, bool transition, float t, float direction,
                             float stretch) {
  ctx->SetTarget(output.bitmap.Get());
  ctx->BeginDraw();
  ctx->Clear(Color(0x1b1d25));
  ctx->PushAxisAlignedClip(D2D1::RectF(0, 64, 480, 480), D2D1_ANTIALIAS_MODE_ALIASED);
  if (transition) {
    float e = 1 - std::pow(1 - t, 3.f);
    ctx->DrawBitmap(previous.bitmap.Get(),
                    D2D1::RectF(-direction * 8 * e, 0, 480 - direction * 8 * e, 480));
    ctx->DrawBitmap(pages[tab].bitmap.Get(),
                    D2D1::RectF(direction * 8 * (1 - e), 0, 480 + direction * 8 * (1 - e), 480), e);
  } else
    ctx->DrawBitmap(pages[tab].bitmap.Get());
  ctx->PopAxisAlignedClip();
  Header(marker, stretch);
  Check(ctx->EndDraw());
  ++rendered;
}

void ScreenRenderer::QueueReadback() {
  for (auto& r : readbacks)
    if (!r.pending) {
      ctx->SetTarget(nullptr);
      gpu->CopyResource(r.texture.Get(), output.texture.Get());
      gpu->End(r.query.Get());
      r.pending = true;
      r.serial = ++serial;
      gpu->Flush();
      return;
    }
}

bool ScreenRenderer::ReadLatest(Frame& frame) {
  bool result = false;
  for (auto& r : readbacks)
    if (r.pending &&
        gpu->GetData(r.query.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
      D3D11_MAPPED_SUBRESOURCE map{};
      auto hr = gpu->Map(r.texture.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
      if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
        continue;
      Check(hr);
      if (r.serial > delivered) {
        for (int y = 0; y < 480; ++y)
          std::memcpy(frame.data() + y * 1920, static_cast<uint8_t*>(map.pData) + y * map.RowPitch,
                      1920);
        delivered = r.serial;
        result = true;
      }
      gpu->Unmap(r.texture.Get(), 0);
      r.pending = false;
    }
  return result;
}
}  // namespace jonsbo
