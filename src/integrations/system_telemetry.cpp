// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "integrations/system_telemetry.hpp"

namespace jonsbo {
DashboardState SystemTelemetry::Read() {
  std::lock_guard lock(mutex);
  return state;
}

void SystemTelemetry::Run(std::stop_token stop) {
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER engines = nullptr, memory = nullptr, cpuUtility = nullptr, cpuFrequency = nullptr,
               cpuPerformance = nullptr;
  if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
    PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engines);
    PdhAddEnglishCounterW(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &memory);
    PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Utility", 0,
                          &cpuUtility);
    PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\Processor Frequency", 0,
                          &cpuFrequency);
    PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Performance", 0,
                          &cpuPerformance);
    PdhCollectQueryData(query);
  }
  auto single = [](PDH_HCOUNTER counter) -> std::optional<double> {
    if (!counter)
      return {};
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS ||
        (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA))
      return {};
    return value.doubleValue;
  };
  auto values = [](PDH_HCOUNTER counter) {
    std::map<std::wstring, double> values;
    if (!counter)
      return values;
    DWORD bytes = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) !=
        PDH_MORE_DATA)
      return values;
    std::vector<BYTE> buffer(bytes);
    auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items) ==
        ERROR_SUCCESS)
      for (DWORD i = 0; i < count; ++i)
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
            items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
          values[items[i].szName] = items[i].FmtValue.doubleValue;
    return values;
  };
  FILETIME idle{}, kernel{}, user{};
  GetSystemTimes(&idle, &kernel, &user);
  auto last = Clock::now();
  uint64_t previousRx = 0, previousTx = 0;
  bool first = true;
  while (!stop.stop_requested()) {
    DashboardState s = Read();
    FILETIME ni{}, nk{}, nu{};
    GetSystemTimes(&ni, &nk, &nu);
    uint64_t total = Ticks(nk) - Ticks(kernel) + Ticks(nu) - Ticks(user);
    double percent = total ? 100. * (total - (Ticks(ni) - Ticks(idle))) / total : 0;
    const bool sampled = query && PdhCollectQueryData(query) == ERROR_SUCCESS;
    if (sampled)
      if (const auto utility = single(cpuUtility))
        percent = *utility;
    percent = std::clamp(percent, 0., 100.);
    const auto temperature = temperatures.Read();
    s.cpuPercent = float(percent);
    s.cpuTemperature = temperature.cpu;
    s.gpuTemperature = temperature.gpu;
    s.cpuFrequencyMhz.reset();
    if (sampled) {
      const auto frequency = single(cpuFrequency);
      const auto performance = single(cpuPerformance);
      if (frequency && performance && *frequency > 0 && *performance > 0)
        s.cpuFrequencyMhz = float(*frequency * *performance / 100.);
      else if (frequency && *frequency > 0)
        s.cpuFrequencyMhz = float(*frequency);
    }
    s.cpu = Fixed(percent) + L"%";
    s.cpuDetail = temperature.cpu ? Fixed(*temperature.cpu) + L"℃" : L"—℃";
    MEMORYSTATUSEX ram{sizeof(ram)};
    if (GlobalMemoryStatusEx(&ram))
      s.cpuDetail +=
          L"  ·  " + Fixed(double(ram.ullTotalPhys - ram.ullAvailPhys) / 1e9, 1) + L" GB";
    s.gpuDetail = temperature.gpu ? Fixed(*temperature.gpu) + L"℃" : L"—℃";
    if (sampled) {
      std::map<std::wstring, double> totals;
      for (const auto& [name, v] : values(engines)) {
        auto start = name.find(L"luid_");
        if (start != std::wstring::npos)
          totals[name.substr(start)] += v;
      }
      if (!totals.empty()) {
        double busy = 0;
        for (const auto& [_, v] : totals)
          busy = std::max(busy, v);
        busy = std::clamp(busy, 0., 100.);
        s.gpu = Fixed(busy) + L"%";
        std::move(s.gpuHistory.begin() + 1, s.gpuHistory.end(), s.gpuHistory.begin());
        s.gpuHistory.back() = float(busy);
      }
      auto memoryValues = values(memory);
      if (!memoryValues.empty()) {
        double bytes = 0;
        for (const auto& [_, v] : memoryValues)
          bytes += v;
        s.gpuDetail += L"  ·  " + Fixed(bytes / 1e9, 1) + L" GB";
      }
    }
    std::move(s.cpuHistory.begin() + 1, s.cpuHistory.end(), s.cpuHistory.begin());
    s.cpuHistory.back() = float(percent);
    idle = ni;
    kernel = nk;
    user = nu;
    ULARGE_INTEGER free{}, all{};
    if (GetDiskFreeSpaceExW(L"C:\\", &free, &all, nullptr) && all.QuadPart) {
      s.diskFraction = 1.f - float(double(free.QuadPart) / all.QuadPart);
      s.disk = Fixed(s.diskFraction * 100) + L"%";
      s.diskDetail = Fixed(double(all.QuadPart - free.QuadPart) / 1e9) + L" / " +
                     Fixed(double(all.QuadPart) / 1e9) + L" GB";
    }
    PMIB_IF_TABLE2 table = nullptr;
    uint64_t rx = 0, tx = 0;
    if (GetIfTable2(&table) == NO_ERROR) {
      for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto& row = table->Table[i];
        if (row.OperStatus == IfOperStatusUp && row.Type != IF_TYPE_SOFTWARE_LOOPBACK) {
          rx += row.InOctets;
          tx += row.OutOctets;
        }
      }
      FreeMibTable(table);
    }
    double elapsed = std::chrono::duration<double>(Clock::now() - last).count();
    if (!first && elapsed > 0 && rx >= previousRx && tx >= previousTx) {
      s.download = L"↓ " + BitRate((rx - previousRx) / elapsed);
      s.upload = L"↑ " + BitRate((tx - previousTx) / elapsed);
    }
    previousRx = rx;
    previousTx = tx;
    last = Clock::now();
    first = false;
    if (limits) {
      s.quotas.clear();
      for (const auto& w : limits->Read().windows) {
        std::wstring title = w.minutes == 300                           ? L"5 小时"
                             : w.minutes == 10080                       ? L"1 周"
                             : w.minutes >= 40320 && w.minutes <= 44640 ? L"1 月"
                             : w.minutes % 1440 == 0 ? std::to_wstring(w.minutes / 1440) + L" 天"
                                                     : std::to_wstring(w.minutes) + L" 分钟";
        s.quotas.push_back({title, ResetCountdown(w.resetsAt), 100 - w.used});
      }
    }
    {
      std::lock_guard lock(mutex);
      state = s;
    }
    for (int i = 0; i < 10 && !stop.stop_requested(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (query)
    PdhCloseQuery(query);
}
}  // namespace jonsbo
