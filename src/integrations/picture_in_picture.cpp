// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "integrations/picture_in_picture.hpp"

namespace jonsbo {
int PictureInPicture::Detect() {
  HWND found = nullptr;
  EnumWindows(
      [](HWND w, LPARAM data) -> BOOL {
        if (!IsWindowVisible(w))
          return TRUE;
        wchar_t title[256];
        GetWindowTextW(w, title, 256);
        std::wstring name(title);
        if (name != L"画中画" && name != L"Picture in picture" && name != L"Picture-in-Picture" &&
            name != L"Picture in Picture")
          return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
          return TRUE;
        wchar_t file[32768];
        DWORD length = 32768;
        bool browser = false;
        if (QueryFullProcessImageNameW(process, 0, file, &length)) {
          auto exe = std::filesystem::path(file).filename().wstring();
          browser = _wcsicmp(exe.c_str(), L"chrome.exe") == 0 ||
                    _wcsicmp(exe.c_str(), L"msedge.exe") == 0 ||
                    _wcsicmp(exe.c_str(), L"firefox.exe") == 0;
        }
        CloseHandle(process);
        if (browser) {
          *reinterpret_cast<HWND*>(data) = w;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&found));
  if (found == window)
    return 0;
  bool was = window != nullptr;
  Stop();
  if (!found)
    return was ? -1 : 0;
  try {
    using namespace winrt::Windows::Graphics::Capture;
    auto interop =
        winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    Check(interop->CreateForWindow(found, winrt::guid_of<GraphicsCaptureItem>(),
                                   winrt::put_abi(item)));
    size = item.Size();
    pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        direct, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
        size);
    session = pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);
    session.StartCapture();
    window = found;
    error.clear();
    return 1;
  } catch (const winrt::hresult_error& e) {
    error = e.message().c_str();
    Stop();
    return was ? -1 : 0;
  }
}

bool PictureInPicture::Read(ID3D11DeviceContext* context, ComPtr<ID3D11Texture2D>& texture,
                            D2D1_RECT_F& source) {
  if (!pool || !window || IsIconic(window))
    return false;
  try {
    auto frame = pool.TryGetNextFrame();
    if (!frame)
      return false;
    for (int i = 0; i < 2; ++i) {
      auto next = pool.TryGetNextFrame();
      if (!next)
        break;
      frame.Close();
      frame = next;
    }
    auto content = frame.ContentSize();
    auto access = frame.Surface()
                      .as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<ID3D11Texture2D> input;
    Check(access->GetInterface(IID_PPV_ARGS(&input)));
    D3D11_TEXTURE2D_DESC desc{};
    input->GetDesc(&desc);
    if (!texture || desc.Width != width || desc.Height != height) {
      width = desc.Width;
      height = desc.Height;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.CPUAccessFlags = 0;
      desc.MiscFlags = 0;
      desc.Usage = D3D11_USAGE_DEFAULT;
      texture.Reset();
      Check(device->CreateTexture2D(&desc, nullptr, &texture));
    }
    context->CopyResource(texture.Get(), input.Get());
    source = D2D1::RectF(0, 0, float(std::min(int(width), content.Width)),
                         float(std::min(int(height), content.Height)));
    RECT bounds{}, client{};
    POINT origin{};
    if (SUCCEEDED(
            DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) &&
        GetClientRect(window, &client) && ClientToScreen(window, &origin)) {
      float left = float(origin.x - bounds.left), top = float(origin.y - bounds.top);
      if (left >= 0 && top >= 0 && client.right > 0 && client.bottom > 0) {
        source.left = left;
        source.top = top;
        source.right = std::min(source.right, left + client.right);
        source.bottom = std::min(source.bottom, top + client.bottom);
      }
    }
    frame.Close();
    if (content.Width != size.Width || content.Height != size.Height) {
      size = content;
      pool.Recreate(direct,
                    winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    2, size);
    }
    return source.right > source.left && source.bottom > source.top;
  } catch (const winrt::hresult_error& e) {
    error = e.message().c_str();
    return false;
  }
}

bool PictureInPicture::Active() const {
  return window != nullptr;
}

const std::wstring& PictureInPicture::Error() const {
  return error;
}

void PictureInPicture::Stop() {
  try {
    if (session)
      session.Close();
    if (pool)
      pool.Close();
  } catch (...) {
  }
  session = nullptr;
  pool = nullptr;
  item = nullptr;
  window = nullptr;
  width = height = 0;
}
}  // namespace jonsbo
