// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/utilities.hpp"

namespace jonsbo {
std::wstring CodexExecutable(const std::filesystem::path& config);
std::wstring ResetCountdown(int64_t timestamp);

}  // namespace jonsbo
