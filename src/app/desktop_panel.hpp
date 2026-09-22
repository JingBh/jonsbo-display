// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"
namespace jonsbo {
int PanelHit(float x, float y);
void PanelPoint(HWND window, LPARAM position, float& x, float& y);
void PlacePanel(HWND window);
void MaintainDesktopLayer(HWND window);
}  // namespace jonsbo
