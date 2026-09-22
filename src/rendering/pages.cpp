// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "rendering/screen_renderer.hpp"

namespace jonsbo {
void ScreenRenderer::DrawPage(int tab, const DashboardState& s) {
  ctx->SetTarget(pages[tab].bitmap.Get());
  ctx->BeginDraw();
  ctx->SetTransform(D2D1::Matrix3x2F::Identity());
  Background(tab);
  if (tab == 0) {
    std::array<std::wstring, 4> label{L"CPU", L"GPU", L"硬盘", L"网络"},
        symbols{L"cpu", L"gpu", L"hard-drive", L"network"};
    std::array<std::wstring, 4> value =
        std::array<std::wstring, 4>{s.cpu, s.gpu, s.disk, s.download};
    std::array<std::wstring, 4> detail =
        std::array<std::wstring, 4>{s.cpuDetail, s.gpuDetail, s.diskDetail, L"实时流量"};
    for (int i = 0; i < 4; ++i) {
      float y = 116 + i * 92.f;
      Icon(symbols[i], 32, y - 18, 42, 0xb4b9d5);
      Text(label[i], 90, y, 19);
      Text(detail[i], 90, y + 30, 14, 0xa8a6b4);
      Text(value[i], 448, y + 4, i == 3 ? 23.f : 32.f, 0xe8e7ed, 400, 2);
      if (i == 0)
        Spark(s.cpuHistory, y, 0xb4c4e8);
      if (i == 1)
        Spark(s.gpuHistory, y, 0xc6b9df);
      if (i == 2) {
        Round(252, y + 2, 88, 3, 1.5f, 0xffffff, .086f);
        Round(252, y + 2, 88 * (s.diskFraction), 3, 1.5f, 0xa6c5c5);
      }
      if (i == 3)
        Text(s.upload, 448, y + 30, 16, 0xaaa8b6, 400, 2);
      if (i < 3)
        Rect(32, y + 53, 416, 1, 0xffffff, .0353f);
    }
  } else if (tab == 1) {
    auto card = [&](float x, float y, float width) {
      Round(x, y - 38, width, 76, 14, 0xffffff, .032f);
      brush->SetColor(Color(0xffffff, .065f));
      ctx->DrawRoundedRectangle(
          D2D1::RoundedRect(D2D1::RectF(x, y - 38, x + width, y + 38), 14, 14), brush.Get(), .75f);
    };
    for (int i = 0; i < 2; ++i) {
      float x = 32 + i * 214.f;
      unsigned color = i ? 0xb4c9e7 : 0xc3c4df;
      card(x, 128, 202);
      Icon(i ? L"droplets" : L"thermometer", x + 16, 114, 27, color);
      Text(i ? (home.humidity + L"%") : (home.temperature + L"℃"), x + 186, 140, 29, color, 400, 2);
    }
    if (!home.connected)
      Icon(L"circle-question-mark", 230, 68, 20, 0xaba0b4);
    auto cell = [&](const wchar_t* icon, const HomeState::Device& d, float x, float y, float width,
                    unsigned accent, bool sheer = false, float rotation = 0,
                    const wchar_t* name = nullptr) {
      card(x, y, width);
      unsigned color = !d.available ? 0x66687d : d.active ? accent : 0x85869d;
      Icon(icon, x + 16, y - 13.5f, 27, color, 1, sheer, rotation);
      Text(d.value, x + width - 16, y, 22, color, 700, 2, true);
      if (name)
        Text(name, x + 51, y, 22, color, 700, 0, true);
    };
    std::array<const wchar_t*, 3> lights{L"lamp-ceiling", L"tube-lotion", L"rectangle-ellipsis"};
    for (int i = 0; i < 3; ++i) {
      auto d = home.lights[i];
      cell(lights[i], d, 32 + i * 143.f, 224, 130, ReadableLightColor(d.color), false,
           i == 1 ? 180.f : 0.f);
    }
    for (int i = 0; i < 2; ++i) {
      auto d = home.covers[i];
      cell(L"blinds", d, 32 + i * 214.f, 320, 202, 0xcbb5ef, i == 1, 0, i ? L"纱帘" : L"窗帘");
    }
    for (int i = 0; i < 2; ++i) {
      auto d = home.climate[i];
      unsigned accent = i                                             ? 0xe7b99d
                        : d.value.find(L"制冷") != std::wstring::npos ? 0xa8cdec
                        : d.value.find(L"制热") != std::wstring::npos ? 0xe7b99d
                        : d.value.find(L"除湿") != std::wstring::npos ? 0xcbb5ef
                        : d.value.find(L"送风") != std::wstring::npos ? 0xadd9c5
                                                                      : 0xd4cbea;
      auto split = d.value.find(L' ');
      bool hasTemperature = split != std::wstring::npos;
      if (hasTemperature) {
        d.value = d.value.substr(split + 1);
        if (!d.value.empty() && d.value.back() == L'°')
          d.value.back() = L'℃';
      }
      cell(i ? L"heater" : L"air-vent", d, 32 + i * 214.f, 416, 202, accent, false, 0,
           i ? L"地暖" : L"空调");
    }
  } else if (tab == 2) {
    auto quotas = s.quotas;
    if (quotas.empty())
      Text(L"额度暂不可用", 240, 276, 24, 0x9393a8, 400, 1);
    for (size_t i = 0; i < quotas.size(); ++i) {
      bool single = quotas.size() == 1;
      float y = single ? 230.f : 120 + i * 214.f, baseline = y + 70, bar = baseline + 20;
      auto& q = quotas[i];
      auto& motion = quotaMotion[q.title];
      double now = ClockSeconds();
      motion.Set(q.remaining, now, reducedMotion || !hwnd);
      double remaining = motion.At(now);
      Text(q.title + L"剩余", 32, y, 24, 0xd9d8e9);
      RollingPercent(remaining, 32, baseline, 58, motion.Active(now));
      Text(q.reset, 448, baseline, 16, 0xb6b1ca, 400, 2);
      Round(32, bar, 416, 5, 2.5f, 0xffffff, .10f);
      Round(32, bar, float(416 * remaining / 100), 5, 2.5f, i ? 0xc5ade8 : 0x9dbcf2);
    }
  } else if (tab == 3) {
    if (!media.playing) {
      Text(L"暂无播放内容", 240, 278, 22, 0x9292a8, 400, 1);
      Check(ctx->EndDraw());
      ctx->SetTarget(nullptr);
      gpu->CopyResource(musicBase.texture.Get(), pages[3].texture.Get());
      return;
    }
    auto cover = mediaArt.Get();
    {
      ctx->Clear(Color(0x1b1d25));
      if (mediaArt && mediaBlur) {
        auto layer = D2D1::LayerParameters1();
        layer.opacity = .18f;
        ctx->PushLayer(layer, nullptr);
        ctx->DrawImage(mediaBlur.Get(), D2D1::Point2F(0, 0));
        ctx->PopLayer();
      }
      Gradient(D2D1::RectF(0, 56, 480, 480),
               {{0, Color(0x191b23, .2f)}, {.55f, Color(0x191b23, 0)}, {1, Color(0x181b25, .85f)}},
               {0, 56}, {0, 480});
    }
    // Cached GPU blur reproduces the soft cover shadow.
    if (cover) {
      ComPtr<ID2D1Effect> shadow;
      Check(ctx->CreateEffect(CLSID_D2D1Shadow, &shadow));
      shadow->SetInput(0, cover);
      shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, 12.f);
      shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(0, 0, 0, .45f));
      auto size = cover->GetSize();
      ctx->SetTransform(D2D1::Matrix3x2F::Scale(232 / size.width, 232 / size.height) *
                        D2D1::Matrix3x2F::Translation(124, 94));
      ctx->DrawImage(shadow.Get());
      ctx->SetTransform(D2D1::Matrix3x2F::Identity());
      float side = std::min(size.width, size.height);
      ClipRound(D2D1::RectF(124, 84, 356, 316), 18, [&] {
        ctx->DrawBitmap(cover, D2D1::RectF(124, 84, 356, 316), 1,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                        D2D1::RectF((size.width - side) / 2, (size.height - side) / 2,
                                    (size.width + side) / 2, (size.height + side) / 2));
      });
    } else
      Round(124, 84, 232, 232, 18, 0x282a34);
    brush->SetColor(Color(0xffffff, .188f));
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(124, 84, 356, 316), 18, 18),
                              brush.Get(), 1);
    ctx->PushAxisAlignedClip(D2D1::RectF(24, 334, 456, 412), D2D1_ANTIALIAS_MODE_ALIASED);
    Text(media.title, 240, 364, 28, 0xf5f5f7, 400, 1);
    Text(media.artist, 240, 400, 17, 0xb2aeba, 400, 1);
    ctx->PopAxisAlignedClip();
  } else {
    Rect(0, 56, 480, 424, 0);
    if (pipBitmap) {
      float w = pipSource.right - pipSource.left, h = pipSource.bottom - pipSource.top,
            scale = std::min(480 / w, 270 / h), width = w * scale, height = h * scale;
      ctx->DrawBitmap(pipBitmap.Get(),
                      D2D1::RectF((480 - width) / 2, 133 + (270 - height) / 2, (480 + width) / 2,
                                  133 + (270 + height) / 2),
                      1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, pipSource);
    }
  }
  Check(ctx->EndDraw());
  if (tab == 3) {
    ctx->SetTarget(nullptr);
    gpu->CopyResource(musicBase.texture.Get(), pages[3].texture.Get());
    UpdateMusicProgress();
  }
}
}  // namespace jonsbo
