// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "rendering/graphics.hpp"
#include <dwmapi.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

namespace jonsbo {
// Capture only recognized browser PiP windows, never the desktop or arbitrary tabs.
class PictureInPicture {
 public:
  explicit PictureInPicture(ID3D11Device* native) : device(native) {
    ComPtr<IDXGIDevice> dxgi;
    Check(native->QueryInterface(IID_PPV_ARGS(&dxgi)));
    winrt::com_ptr<IInspectable> inspectable;
    Check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
    direct = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
  }
  ~PictureInPicture() {
    Stop();
  }
  int Detect();
  bool Read(ID3D11DeviceContext* context, ComPtr<ID3D11Texture2D>& texture, D2D1_RECT_F& source);
  bool Active() const;
  const std::wstring& Error() const;

 private:
  void Stop();
  ID3D11Device* device;
  HWND window = nullptr;
  UINT width = 0, height = 0;
  std::wstring error;
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice direct{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
  winrt::Windows::Graphics::SizeInt32 size{};
};

}  // namespace jonsbo
