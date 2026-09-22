// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "integrations/hardware_monitor.hpp"
#include "core/utilities.hpp"

namespace jonsbo {
HardwareMonitor::HardwareMonitor() {
  const auto path = ExeDir() / L"jonsbo-hardware-monitor.dll";
  module = LoadLibraryExW(path.c_str(), nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!module)
    return;
  auto create = reinterpret_cast<CreateFn>(GetProcAddress(module, "JonsboMonitorCreate"));
  read = reinterpret_cast<ReadFn>(GetProcAddress(module, "JonsboMonitorRead"));
  destroy = reinterpret_cast<DestroyFn>(GetProcAddress(module, "JonsboMonitorDestroy"));
  if (create && read && destroy)
    instance = create();
  if (!instance) {
    read = nullptr;
    destroy = nullptr;
    FreeLibrary(module);
    module = nullptr;
  }
}

HardwareMonitor::~HardwareMonitor() {
  if (instance && destroy)
    destroy(instance);
  if (module)
    FreeLibrary(module);
}

Temperatures HardwareMonitor::Read() const {
  Temperatures result;
  if (!instance || !read)
    return result;
  float cpu = NAN, gpu = NAN;
  if (!read(instance, &cpu, &gpu))
    return result;
  if (std::isfinite(cpu))
    result.cpu = cpu;
  if (std::isfinite(gpu))
    result.gpu = gpu;
  return result;
}
}  // namespace jonsbo
