// SPDX-License-Identifier: MPL-2.0
#using < LibreHardwareMonitorLib.dll>
#include <cmath>
#include <limits>
#include <vcclr.h>

using namespace System;
using namespace System::Runtime::InteropServices;
using namespace LibreHardwareMonitor::Hardware;

namespace {
ref class MonitorState sealed {
 public:
  MonitorState() {
    computer = gcnew Computer();
    computer->IsCpuEnabled = true;
    computer->IsGpuEnabled = true;
    computer->Open();
  }

  ~MonitorState() {
    this->!MonitorState();
  }
  !MonitorState() {
    if (computer != nullptr) {
      computer->Close();
      computer = nullptr;
    }
  }

  bool Read(float % cpu, float % gpu) {
    cpu = Single::NaN;
    gpu = Single::NaN;
    int cpuPriority = 0;
    int gpuPriority = 0;
    try {
      for each (IHardware ^ hardware in computer->Hardware)
        ReadHardware(hardware, cpu, gpu, cpuPriority, gpuPriority);
      return true;
    } catch (Exception ^) {
      return false;
    }
  }

 private:
  static void ReadHardware(IHardware ^ hardware, float % cpu, float % gpu, int % cpuPriority,
                           int % gpuPriority) {
    hardware->Update();
    const bool isCpu = hardware->HardwareType == HardwareType::Cpu;
    const bool isGpu = hardware->HardwareType == HardwareType::GpuAmd ||
                       hardware->HardwareType == HardwareType::GpuIntel ||
                       hardware->HardwareType == HardwareType::GpuNvidia;
    for each (ISensor ^ sensor in hardware->Sensors) {
      if (sensor->SensorType != SensorType::Temperature || !sensor->Value.HasValue)
        continue;
      const float value = sensor->Value.Value;
      if (Single::IsNaN(value) || Single::IsInfinity(value) || value <= 1 || value >= 150)
        continue;
      String ^ name = sensor->Name;
      if (isCpu) {
        int priority = name->IndexOf("Package", StringComparison::OrdinalIgnoreCase) >= 0 ? 3
                       : name->IndexOf("Tctl", StringComparison::OrdinalIgnoreCase) >= 0  ? 2
                                                                                          : 1;
        if (priority > cpuPriority) {
          cpu = value;
          cpuPriority = priority;
        }
      } else if (isGpu) {
        int priority = name->Equals("GPU Core", StringComparison::OrdinalIgnoreCase)     ? 3
                       : name->IndexOf("Core", StringComparison::OrdinalIgnoreCase) >= 0 ? 2
                                                                                         : 1;
        if (priority > gpuPriority) {
          gpu = value;
          gpuPriority = priority;
        }
      }
    }
    for each (IHardware ^ child in hardware->SubHardware)
      ReadHardware(child, cpu, gpu, cpuPriority, gpuPriority);
  }

  Computer ^ computer;
};

MonitorState ^ FromHandle(void* value) {
  if (!value)
    return nullptr;
  GCHandle handle = GCHandle::FromIntPtr(IntPtr(value));
  return dynamic_cast<MonitorState ^>(handle.Target);
}
}  // namespace

extern "C" __declspec(dllexport) void* __cdecl JonsboMonitorCreate() {
  try {
    GCHandle handle = GCHandle::Alloc(gcnew MonitorState());
    return GCHandle::ToIntPtr(handle).ToPointer();
  } catch (Exception ^) {
    return nullptr;
  }
}

extern "C" __declspec(dllexport) bool __cdecl JonsboMonitorRead(void* instance, float* cpu,
                                                                float* gpu) {
  if (!cpu || !gpu)
    return false;
  try {
    MonitorState ^ monitor = FromHandle(instance);
    if (monitor == nullptr)
      return false;
    float managedCpu = Single::NaN;
    float managedGpu = Single::NaN;
    bool ok = monitor->Read(managedCpu, managedGpu);
    *cpu = managedCpu;
    *gpu = managedGpu;
    return ok;
  } catch (Exception ^) {
    return false;
  }
}

extern "C" __declspec(dllexport) void __cdecl JonsboMonitorDestroy(void* instance) {
  if (!instance)
    return;
  try {
    GCHandle handle = GCHandle::FromIntPtr(IntPtr(instance));
    delete dynamic_cast<MonitorState ^>(handle.Target);
    handle.Free();
  } catch (Exception ^) {
  }
}
