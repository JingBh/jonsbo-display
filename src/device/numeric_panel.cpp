// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "device/numeric_panel.hpp"
#include <hidsdi.h>
#include <setupapi.h>
#include <utility>

namespace jonsbo {
namespace {
constexpr USHORT kVendorId = 0x5131;
constexpr USHORT kProductId = 0x2007;

uint8_t Byte(double value) {
  return static_cast<uint8_t>(std::clamp(std::lround(value), 0l, 255l));
}

void Temperature(std::array<uint8_t, 64>& report, size_t offset,
                 const std::optional<float>& value) {
  if (!value || !std::isfinite(*value))
    return;
  double integral = 0;
  const double fractional = std::modf(std::clamp<double>(*value, 0, 255), &integral);
  report[offset] = Byte(integral);
  report[offset + 1] = Byte(fractional * 100);
}

struct HidHandle {
  HANDLE value = INVALID_HANDLE_VALUE;
  USHORT reportLength = 0;
  HidHandle() = default;
  HidHandle(HANDLE handle, USHORT length) : value(handle), reportLength(length) {}
  HidHandle(const HidHandle&) = delete;
  HidHandle& operator=(const HidHandle&) = delete;
  HidHandle(HidHandle&& other) noexcept
      : value(std::exchange(other.value, INVALID_HANDLE_VALUE)),
        reportLength(std::exchange(other.reportLength, 0)) {}
  HidHandle& operator=(HidHandle&& other) noexcept {
    if (this != &other) {
      if (value != INVALID_HANDLE_VALUE)
        CloseHandle(value);
      value = std::exchange(other.value, INVALID_HANDLE_VALUE);
      reportLength = std::exchange(other.reportLength, 0);
    }
    return *this;
  }
  ~HidHandle() {
    if (value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};

std::vector<HidHandle> OpenPanels() {
  GUID hidGuid{};
  HidD_GetHidGuid(&hidGuid);
  HDEVINFO devices =
      SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (devices == INVALID_HANDLE_VALUE)
    return {};
  std::vector<HidHandle> result;
  for (DWORD index = 0;; ++index) {
    SP_DEVICE_INTERFACE_DATA interfaceData{sizeof(interfaceData)};
    if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &hidGuid, index, &interfaceData))
      break;
    DWORD bytes = 0;
    SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0, &bytes, nullptr);
    if (!bytes)
      continue;
    std::vector<uint8_t> buffer(bytes);
    auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, bytes, nullptr, nullptr))
      continue;
    HANDLE handle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE)
      continue;
    HIDD_ATTRIBUTES attributes{sizeof(attributes)};
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps{};
    const bool matching = HidD_GetAttributes(handle, &attributes) &&
                          attributes.VendorID == kVendorId && attributes.ProductID == kProductId;
    const bool described = matching && HidD_GetPreparsedData(handle, &preparsed) &&
                           HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
    if (preparsed)
      HidD_FreePreparsedData(preparsed);
    if (!described || caps.OutputReportByteLength < 65) {
      CloseHandle(handle);
      continue;
    }
    result.emplace_back(handle, caps.OutputReportByteLength);
  }
  SetupDiDestroyDeviceInfoList(devices);
  return result;
}

bool Send(HidHandle& device, const std::array<uint8_t, 64>& payload) {
  std::vector<uint8_t> report(device.reportLength);
  std::copy(payload.begin(), payload.end(), report.begin() + 1);
  DWORD written = 0;
  if (WriteFile(device.value, report.data(), DWORD(report.size()), &written, nullptr) &&
      written == report.size())
    return true;
  return HidD_SetOutputReport(device.value, report.data(), ULONG(report.size())) != FALSE;
}
}  // namespace

std::array<uint8_t, 64> EncodeNumericPanelReport(const NumericPanelMetrics& metrics,
                                                  const SYSTEMTIME& time) {
  std::array<uint8_t, 64> report{};
  report[0] = 0;
  report[1] = 1;
  report[2] = 2;
  Temperature(report, 3, metrics.cpuTemperature);
  report[5] = 0;  // Celsius.
  report[6] = Byte(metrics.cpuPercent);
  if (metrics.cpuFrequencyMhz && std::isfinite(*metrics.cpuFrequencyMhz)) {
    const long mhz = std::clamp(std::lround(*metrics.cpuFrequencyMhz), 0l, 25599l);
    report[9] = uint8_t(mhz / 100);
    report[10] = uint8_t(mhz % 100);
  }
  Temperature(report, 13, metrics.gpuTemperature);
  report[15] = 0;  // Celsius.
  report[25] = uint8_t(time.wYear / 100);
  report[26] = uint8_t(time.wYear % 100);
  report[27] = uint8_t(time.wMonth);
  report[28] = uint8_t(time.wDay);
  report[29] = uint8_t(time.wHour);
  report[30] = uint8_t(time.wMinute);
  report[31] = uint8_t(time.wSecond);
  report[32] = uint8_t(time.wDayOfWeek);
  return report;
}

NumericPanelTransport::~NumericPanelTransport() {
  worker.request_stop();
  if (worker.joinable())
    worker.join();
}

void NumericPanelTransport::Start() {
  if (worker.joinable())
    return;
  worker = std::jthread([this](std::stop_token stop) { Run(stop); });
}

void NumericPanelTransport::Restart() {
  worker.request_stop();
  if (worker.joinable())
    worker.join();
  {
    std::lock_guard lock(mutex);
    status.clear();
  }
  Start();
}

void NumericPanelTransport::Update(const NumericPanelMetrics& value) {
  std::lock_guard lock(mutex);
  metrics = value;
}

std::wstring NumericPanelTransport::Status() {
  std::lock_guard lock(mutex);
  return status;
}

void NumericPanelTransport::Run(std::stop_token stop) {
  std::vector<HidHandle> panels;
  auto nextOpen = std::chrono::steady_clock::now();
  while (!stop.stop_requested()) {
    const auto now = std::chrono::steady_clock::now();
    if (panels.empty() && now >= nextOpen) {
      panels = OpenPanels();
      nextOpen = now + std::chrono::seconds(2);
      std::lock_guard lock(mutex);
      status = panels.empty() ? L"Numeric panel 5131:2007 not available" : L"";
    }
    NumericPanelMetrics snapshot;
    {
      std::lock_guard lock(mutex);
      snapshot = metrics;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    const auto report = EncodeNumericPanelReport(snapshot, time);
    bool failed = false;
    for (auto& panel : panels) {
      if (Send(panel, report))
        ++accepted;
      else
        failed = true;
    }
    if (failed) {
      panels.clear();
      std::lock_guard lock(mutex);
      status = L"Numeric panel write failed";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  std::array<uint8_t, 64> close{};
  close[1] = 15;
  for (auto& panel : panels)
    Send(panel, close);
}
}  // namespace jonsbo
