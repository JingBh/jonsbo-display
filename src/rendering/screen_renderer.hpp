// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/dashboard_state.hpp"
#include "device/frame.hpp"
#include "integrations/home_assistant.hpp"
#include "integrations/picture_in_picture.hpp"
#include "integrations/system_media.hpp"
#include "rendering/graphics.hpp"
#include "rendering/media_playhead.hpp"
#include "rendering/quota_animation.hpp"

namespace jonsbo {
class ScreenRenderer {
 public:
  ScreenRenderer(HWND window, const std::filesystem::path& assets);
  Surface CreateSurface();
  void SetMedia(const MediaState& value);
  static std::wstring Duration(double seconds);
  void MusicProgress();
  void UpdateMusicProgress();
  int DetectPictureInPicture();
  bool UpdatePictureInPicture();
  bool HasPictureInPicture() const;
  void SetHome(const HomeState& value);
  void SetReducedMotion(bool value);
  bool QuotaAnimating() const;
  void Icon(const std::wstring& name, float x, float y, float size, unsigned color = 0xb8b8cf,
            float opacity = 1, bool sheer = false, float rotation = 0);
  void RollingPercent(double value, float x, float baseline, float size, bool animate);
  void Rect(float x, float y, float w, float h, unsigned color, float alpha = 1);
  void Round(float x, float y, float w, float h, float r, unsigned color, float alpha = 1);
  void Text(const std::wstring& text, float x, float baseline, float size,
            unsigned color = 0xf5f5f7, int weight = 400, int align = 0, bool centerY = false);
  void Gradient(D2D1_RECT_F rect, std::initializer_list<D2D1_GRADIENT_STOP> stops, D2D1_POINT_2F a,
                D2D1_POINT_2F b, bool stroke = false, float radius = 0);
  void ClipRound(D2D1_RECT_F r, float radius, std::function<void()> draw);
  void Background(int tab);
  void RenderTwilight();
  void Line(float x, float y, float xx, float yy, unsigned c, float alpha = 1, float width = 1.3f);
  void Spark(const std::array<float, 12>& values, float y, unsigned color);
  void DrawPage(int tab, const DashboardState& s);
  // Lucide SVG documents are cached and rendered directly by Direct2D.
  void Header(float position, float stretch);
  void Snapshot();
  void Compose(int tab, float marker, bool transition, float t, float direction, float stretch);
  void CreatePanelTarget();
  void Present(int active, int hover, bool force = false);
  void QueueReadback();
  bool ReadLatest(Frame& frame);
  uint64_t rendered = 0;

 private:
  int controlActive = -1, controlHover = -1;
  UINT panelWidth = 480, panelHeight = 88;
  ComPtr<IDCompositionDevice> composition;
  ComPtr<IDCompositionTarget> compositionTarget;
  ComPtr<IDCompositionVisual> panelVisual;
  HWND hwnd;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> gpu;
  ComPtr<ID2D1Factory1> factory;
  ComPtr<ID2D1Device> d2d;
  ComPtr<ID2D1DeviceContext> ctx;
  ComPtr<IDWriteFactory> write;
  ComPtr<IWICImagingFactory> wic;
  ComPtr<ID2D1SolidColorBrush> brush;
  ComPtr<ID2D1Bitmap1> windowTarget;
  ComPtr<IDXGISwapChain> swap;
  ComPtr<ID2D1StrokeStyle> stroke;
  std::filesystem::path assetRoot;
  ComPtr<ID2D1DeviceContext5> svgContext;
  std::map<std::wstring, ComPtr<ID2D1SvgDocument>> lucide;
  std::map<std::wstring, ComPtr<IDWriteTextLayout>> layouts;
  std::map<std::wstring, QuotaMotion> quotaMotion;
  bool reducedMotion = false;
  Surface output, previous, musicBase, mediaAmbient, twilight;
  std::array<Surface, 5> pages;
  MediaState media;
  MediaPlayhead playhead;
  HomeState home;
  std::shared_ptr<std::vector<uint8_t>> artworkBytes;
  ComPtr<ID2D1Bitmap1> mediaArt;
  ComPtr<ID2D1Effect> mediaBlur;
  std::unique_ptr<PictureInPicture> pip;
  ComPtr<ID3D11Texture2D> pipTexture;
  ComPtr<ID2D1Bitmap1> pipBitmap;
  D2D1_RECT_F pipSource{};
  struct Readback {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11Query> query;
    bool pending = false;
    uint64_t serial = 0;
  };
  std::array<Readback, 3> readbacks;
  uint64_t serial = 0, delivered = 0;
};

}  // namespace jonsbo
