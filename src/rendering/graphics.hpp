// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/utilities.hpp"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_3.h>
#include <dcomp.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>

namespace jonsbo {
using Microsoft::WRL::ComPtr;
inline D2D1_COLOR_F Color(unsigned rgb, float a = 1) {
  return D2D1::ColorF(rgb, a);
}
struct Surface {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID2D1Bitmap1> bitmap;
};

}  // namespace jonsbo
