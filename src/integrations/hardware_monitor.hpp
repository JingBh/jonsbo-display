// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/platform.hpp"

namespace jonsbo {
struct Temperatures {
  std::optional<float> cpu;
  std::optional<float> gpu;
};

class HardwareMonitor {
 public:
  HardwareMonitor();
  ~HardwareMonitor();
  HardwareMonitor(const HardwareMonitor&) = delete;
  HardwareMonitor& operator=(const HardwareMonitor&) = delete;
  Temperatures Read() const;

 private:
  using CreateFn = void*(__cdecl*)();
  using ReadFn = bool(__cdecl*)(void*, float*, float*);
  using DestroyFn = void(__cdecl*)(void*);
  HMODULE module = nullptr;
  void* instance = nullptr;
  ReadFn read = nullptr;
  DestroyFn destroy = nullptr;
};
}  // namespace jonsbo
