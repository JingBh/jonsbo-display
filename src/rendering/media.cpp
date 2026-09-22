// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "rendering/screen_renderer.hpp"

namespace jonsbo {
void ScreenRenderer::SetMedia(const MediaState& value) {
  playhead.Set(value.Position(), value.duration, value.playing, value.rate, ClockSeconds(),
               value.identity != media.identity || value.revision != media.revision);
  media = value;
  if (media.artwork == artworkBytes)
    return;
  artworkBytes = media.artwork;
  mediaArt.Reset();
  if (!artworkBytes || artworkBytes->empty())
    return;
  try {
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(artworkBytes->data(), UINT(artworkBytes->size())));
    if (!stream)
      return;
    ComPtr<IWICBitmapDecoder> decoder;
    Check(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                       &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, &frame));
    ComPtr<IWICFormatConverter> converted;
    Check(wic->CreateFormatConverter(&converted));
    Check(converted->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom));
    Check(ctx->CreateBitmapFromWicBitmap(converted.Get(), nullptr, &mediaArt));
    ctx->SetTarget(mediaAmbient.bitmap.Get());
    ctx->BeginDraw();
    ctx->Clear(Color(0x1b1d25));
    auto size = mediaArt->GetSize();
    float scale = std::max(480 / size.width, 480 / size.height);
    ctx->DrawBitmap(mediaArt.Get(),
                    D2D1::RectF((480 - size.width * scale) / 2, (480 - size.height * scale) / 2,
                                (480 + size.width * scale) / 2, (480 + size.height * scale) / 2));
    Check(ctx->EndDraw());
    Check(ctx->CreateEffect(CLSID_D2D1GaussianBlur, &mediaBlur));
    mediaBlur->SetInput(0, mediaAmbient.bitmap.Get());
    mediaBlur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 32.f);
    mediaBlur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
  } catch (...) {
    mediaArt.Reset();
    mediaBlur.Reset();
  }
}

std::wstring ScreenRenderer::Duration(double seconds) {
  int n = int(std::max(0., seconds));
  std::wstring tail = std::to_wstring(n % 60);
  return std::to_wstring(n / 60) + L":" + (tail.size() == 1 ? L"0" : L"") + tail;
}

void ScreenRenderer::MusicProgress() {
  if (!media.playing)
    return;
  double position = playhead.At(ClockSeconds()), duration = media.duration;
  Round(40, 435, 400, 3, 1.5f, 0xffffff, .141f);
  if (duration > 0)
    Round(40, 435, float(400 * position / duration), 3, 1.5f, 0xeeeef2);
  Text(duration > 0 ? Duration(position) : L"--:--", 40, 460, 13, 0xa7a7b2);
  Text(duration > 0 ? L"-" + Duration(duration - position) : L"--:--", 440, 460, 13, 0xa7a7b2, 400,
       2);
}

void ScreenRenderer::UpdateMusicProgress() {
  ctx->SetTarget(pages[3].bitmap.Get());
  ctx->BeginDraw();
  ctx->DrawBitmap(musicBase.bitmap.Get());
  MusicProgress();
  Check(ctx->EndDraw());
}

int ScreenRenderer::DetectPictureInPicture() {
  auto result = pip->Detect();
  if (result != 0)
    pipBitmap.Reset();
  return result;
}

bool ScreenRenderer::UpdatePictureInPicture() {
  if (!pip->Read(gpu.Get(), pipTexture, pipSource))
    return false;
  ComPtr<IDXGISurface> surface;
  Check(pipTexture.As(&surface));
  pipBitmap.Reset();
  auto properties = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_NONE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
  Check(ctx->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &pipBitmap));
  return true;
}

bool ScreenRenderer::HasPictureInPicture() const {
  return pip->Active();
}
}  // namespace jonsbo
