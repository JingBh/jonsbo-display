// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "rendering/screen_renderer.hpp"
#include "resources/resource_ids.h"

namespace jonsbo {
namespace {
int LucideResourceId(std::wstring_view name) {
  static constexpr std::pair<std::wstring_view, int> resources[] = {
      {L"air-vent", IDR_LUCIDE_AIR_VENT},
      {L"blinds", IDR_LUCIDE_BLINDS},
      {L"circle-question-mark", IDR_LUCIDE_QUESTION},
      {L"code-xml", IDR_LUCIDE_CODE},
      {L"cpu", IDR_LUCIDE_CPU},
      {L"droplets", IDR_LUCIDE_DROPLETS},
      {L"gpu", IDR_LUCIDE_GPU},
      {L"hard-drive", IDR_LUCIDE_HARD_DRIVE},
      {L"heater", IDR_LUCIDE_HEATER},
      {L"house", IDR_LUCIDE_HOUSE},
      {L"lamp-ceiling", IDR_LUCIDE_LAMP},
      {L"monitor", IDR_LUCIDE_MONITOR},
      {L"music-2", IDR_LUCIDE_MUSIC},
      {L"network", IDR_LUCIDE_NETWORK},
      {L"picture-in-picture-2", IDR_LUCIDE_PIP},
      {L"power", IDR_LUCIDE_POWER},
      {L"rectangle-ellipsis", IDR_LUCIDE_ELLIPSIS},
      {L"thermometer", IDR_LUCIDE_THERMOMETER},
      {L"tube-lotion", IDR_LUCIDE_TUBE},
  };
  auto found = std::find_if(std::begin(resources), std::end(resources),
                            [name](const auto& item) { return item.first == name; });
  return found == std::end(resources) ? 0 : found->second;
}
}  // namespace

void ScreenRenderer::Icon(const std::wstring& name, float x, float y, float size, unsigned color,
                          float opacity, bool sheer, float rotation) {
  const std::wstring key = name + L"/" + std::to_wstring(color) + (sheer ? L"/sheer" : L"");
  auto found = lucide.find(key);
  if (found == lucide.end()) {
    const int resourceId = LucideResourceId(name);
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource =
        resourceId ? FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA) : nullptr;
    if (!resource)
      throw std::runtime_error("Missing Lucide asset: " + ToUtf8(name));
    HGLOBAL loaded = LoadResource(module, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    const DWORD byteCount = SizeofResource(module, resource);
    if (!bytes || byteCount == 0)
      throw std::runtime_error("Invalid Lucide asset: " + ToUtf8(name));
    std::string source(static_cast<const char*>(bytes), byteCount);
    char hex[10];
    sprintf_s(hex, "#%06x", color);
    size_t at = 0;
    while ((at = source.find("currentColor", at)) != std::string::npos) {
      source.replace(at, 12, hex);
      at += 7;
    }
    if (sheer) {
      auto start = source.find("<svg");
      source.insert(start + 4, " stroke-dasharray=\"1.5 1.5\"");
    }
    ComPtr<IStream> stream;
    stream.Attach(
        SHCreateMemStream(reinterpret_cast<const BYTE*>(source.data()), UINT(source.size())));
    ComPtr<ID2D1SvgDocument> document;
    Check(svgContext->CreateSvgDocument(stream.Get(), D2D1::SizeF(24, 24), &document));
    found = lucide.emplace(key, document).first;
  }
  ComPtr<ID2D1SvgElement> root;
  found->second->GetRoot(&root);
  Check(root->SetAttributeValue(L"opacity", opacity));
  ctx->SetTransform(D2D1::Matrix3x2F::Rotation(rotation, {12, 12}) *
                    D2D1::Matrix3x2F::Scale(size / 24, size / 24) *
                    D2D1::Matrix3x2F::Translation(x, y));
  svgContext->DrawSvgDocument(found->second.Get());
  ctx->SetTransform(D2D1::Matrix3x2F::Identity());
}

void ScreenRenderer::RollingPercent(double value, float x, float baseline, float size,
                                    bool animate) {
  if (!animate) {
    Text(Fixed(value) + L"%", x, baseline, size, 0xf2effb);
    return;
  }
  int digits = value >= 100 ? 3 : value >= 10 ? 2 : 1;
  float advance = size * .61f;
  ctx->PushAxisAlignedClip(
      D2D1::RectF(-4096, baseline - size * 1.08f, 4096, baseline + size * .12f),
      D2D1_ANTIALIAS_MODE_ALIASED);
  for (int i = 0; i < digits; ++i) {
    double place = std::pow(10., digits - i - 1),
           fraction = place == 1 ? value - std::floor(value)
                                 : std::clamp(std::fmod(value, place) - (place - 1), 0., 1.);
    int digit = int(value / place) % 10;
    Text(std::to_wstring(digit), x + i * advance, baseline - float(fraction) * size * 1.2f, size,
         0xf2effb);
    Text(std::to_wstring((digit + 1) % 10), x + i * advance,
         baseline + float(1 - fraction) * size * 1.2f, size, 0xf2effb);
  }
  Text(L"%", x + digits * advance, baseline, size, 0xf2effb);
  ctx->PopAxisAlignedClip();
}

void ScreenRenderer::Rect(float x, float y, float w, float h, unsigned color, float alpha) {
  brush->SetColor(Color(color, alpha));
  ctx->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush.Get());
}

void ScreenRenderer::Round(float x, float y, float w, float h, float r, unsigned color,
                           float alpha) {
  brush->SetColor(Color(color, alpha));
  ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), brush.Get());
}

void ScreenRenderer::Text(const std::wstring& text, float x, float baseline, float size,
                          unsigned color, int weight, int align, bool centerY) {
  const auto key = text + L"|" + std::to_wstring(size) + L"|" + std::to_wstring(weight);
  auto it = layouts.find(key);
  if (it == layouts.end()) {
    if (layouts.size() > 512)
      layouts.clear();
    ComPtr<IDWriteTextFormat> format;
    Check(write->CreateTextFormat(L"Noto Sans CJK SC", nullptr, DWRITE_FONT_WEIGHT(weight),
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                  L"zh-CN", &format));
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteTextLayout> layout;
    Check(write->CreateTextLayout(text.c_str(), UINT32(text.size()), format.Get(), 800, 100,
                                  &layout));
    it = layouts.emplace(key, layout).first;
  }
  DWRITE_TEXT_METRICS m{};
  it->second->GetMetrics(&m);
  DWRITE_LINE_METRICS line{};
  UINT32 count = 0;
  it->second->GetLineMetrics(&line, 1, &count);
  if (align == 1)
    x -= m.width / 2;
  else if (align == 2)
    x -= m.width;
  float top = baseline - line.baseline;
  if (centerY) {
    DWRITE_OVERHANG_METRICS ink{};
    Check(it->second->GetOverhangMetrics(&ink));
    top = baseline - (it->second->GetMaxHeight() + ink.bottom - ink.top) / 2;
  }
  brush->SetColor(Color(color));
  ctx->DrawTextLayout(D2D1::Point2F(x, top), it->second.Get(), brush.Get());
}

void ScreenRenderer::Gradient(D2D1_RECT_F rect, std::initializer_list<D2D1_GRADIENT_STOP> stops,
                              D2D1_POINT_2F a, D2D1_POINT_2F b, bool stroke, float radius) {
  ComPtr<ID2D1GradientStopCollection> collection;
  Check(ctx->CreateGradientStopCollection(stops.begin(), UINT32(stops.size()), &collection));
  ComPtr<ID2D1LinearGradientBrush> gradient;
  Check(ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), collection.Get(),
                                       &gradient));
  if (stroke)
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), gradient.Get(), 1);
  else
    ctx->FillRectangle(rect, gradient.Get());
}

void ScreenRenderer::ClipRound(D2D1_RECT_F r, float radius, std::function<void()> draw) {
  ComPtr<ID2D1RoundedRectangleGeometry> geometry;
  Check(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, radius, radius), &geometry));
  auto params = D2D1::LayerParameters1();
  params.geometricMask = geometry.Get();
  ctx->PushLayer(params, nullptr);
  draw();
  ctx->PopLayer();
}

void ScreenRenderer::Background(int tab) {
  ctx->Clear(Color(0x151827));
  ctx->DrawBitmap(twilight.bitmap.Get());
}

void ScreenRenderer::RenderTwilight() {
  ctx->SetTarget(twilight.bitmap.Get());
  ctx->BeginDraw();
  ctx->Clear(Color(0x131725));
  Gradient(D2D1::RectF(0, 0, 480, 480),
           {{0, Color(0x202944)}, {.52f, Color(0x27203e)}, {1, Color(0x101826)}}, {20, 20},
           {380, 480});
  auto glow = [&](float x, float y, float rx, float ry, unsigned color, float opacity) {
    D2D1_GRADIENT_STOP stops[] = {{0, Color(color, opacity)}, {1, Color(color, 0)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    Check(ctx->CreateGradientStopCollection(stops, 2, &collection));
    ComPtr<ID2D1RadialGradientBrush> radial;
    Check(ctx->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties({x, y}, {0, 0}, rx, ry), collection.Get(), &radial));
    ctx->FillRectangle(D2D1::RectF(0, 0, 480, 480), radial.Get());
  };
  glow(65, 125, 420, 330, 0x365cad, .36f);
  glow(440, 245, 340, 320, 0x8245b8, .27f);
  glow(200, 470, 420, 170, 0x163d61, .22f);
  Check(ctx->EndDraw());
}

void ScreenRenderer::Line(float x, float y, float xx, float yy, unsigned c, float alpha,
                          float width) {
  brush->SetColor(Color(c, alpha));
  ctx->DrawLine({x, y}, {xx, yy}, brush.Get(), width);
}

void ScreenRenderer::Spark(const std::array<float, 12>& values, float y, unsigned color) {
  for (int j = 1; j < 12; ++j)
    Line(252 + (j - 1) * 8, y + 17 - values[j - 1] * .45f, 252 + j * 8, y + 17 - values[j] * .45f,
         color);
}

void ScreenRenderer::Header(float position, float stretch) {
  Rect(0, 0, 480, 64, 0x191b27);
  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t time[20];
  GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &now, L"HH:mm:ss", time, 20);
  Text(time, 32, 42, 28, 0xf5f5f7, 700);
  float x = 216 + position * 50 - stretch / 2, w = 48 + stretch;
  auto rect = D2D1::RectF(x, 10, x + w, 54);
  ClipRound(rect, 15, [&] {
    Round(x, 10, w, 44, 15, 0xffffff, .055f);
    Gradient(
        rect,
        {{0, Color(0xd1cbf4, .12f)}, {.5f, Color(0xffffff, .01f)}, {1, Color(0x6b76bc, .045f)}},
        {x, 10}, {x + w * .4f, 54});
  });
  Gradient(
      rect,
      {{0, Color(0xffffff, .2875f)}, {.45f, Color(0xffffff, .04375f)}, {1, Color(0xffffff, .125f)}},
      {x, 10}, {x + w, 54}, true, 15);
  const wchar_t* navigation[] = {L"monitor", L"house", L"code-xml", L"music-2",
                                 L"picture-in-picture-2"};
  for (int i = 0; i < 5; ++i) {
    float e = std::max(0.f, 1 - std::abs(position - i));
    Icon(navigation[i], 226 + i * 50.f, 18, 28, 0xf5f5fa, .5f + e * .5f);
  }
}
}  // namespace jonsbo
