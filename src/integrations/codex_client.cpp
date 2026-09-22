// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "integrations/codex_client.hpp"

namespace jonsbo {
CodexLimits::Snapshot CodexLimits::Parse(const winrt::Windows::Data::Json::JsonObject& result) {
  using namespace winrt::Windows::Data::Json;
  Snapshot s;
  auto object = [](const JsonObject& parent, const wchar_t* name) -> JsonObject {
    auto v = parent.GetNamedValue(name, nullptr);
    return v && v.ValueType() == JsonValueType::Object ? v.GetObject() : nullptr;
  };
  auto buckets = object(result, L"rateLimitsByLimitId");
  auto limits = buckets ? object(buckets, L"codex") : nullptr;
  if (!limits)
    limits = object(result, L"rateLimits");
  if (limits)
    for (auto name : {L"primary", L"secondary"}) {
      auto w = object(limits, name);
      if (!w)
        continue;
      auto u = w.GetNamedValue(L"usedPercent", nullptr),
           d = w.GetNamedValue(L"windowDurationMins", nullptr),
           r = w.GetNamedValue(L"resetsAt", nullptr);
      if (!u || !d || u.ValueType() != JsonValueType::Number ||
          d.ValueType() != JsonValueType::Number)
        continue;
      double used = u.GetNumber(), duration = d.GetNumber();
      if (!std::isfinite(used) || used < 0 || used > 100 || duration <= 0 || duration > 525600)
        continue;
      s.windows.push_back(
          {used, int(duration),
           r && r.ValueType() == JsonValueType::Number ? int64_t(r.GetNumber()) : 0});
    }
  std::sort(s.windows.begin(), s.windows.end(),
            [](auto a, auto b) { return a.minutes < b.minutes; });
  return s;
}

CodexLimits::Snapshot CodexLimits::Read() {
  std::lock_guard lock(mutex);
  return value;
}

void CodexLimits::Run(std::stop_token stop, const std::wstring& executable) {
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    while (!stop.stop_requested()) {
      PipeProcess child;
      bool ok = child.Start(executable);
      winrt::Windows::Data::Json::JsonObject result;
      if (ok)
        ok = child.Send(
                 "{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":"
                 "\"jonsbo-display\",\"version\":\"0.2.0\"}}}\n") &&
             child.Reply(1, stop, result);
      if (ok)
        ok = child.Send("{\"method\":\"initialized\",\"params\":{}}\n");
      int id = 2;
      while (ok && !stop.stop_requested()) {
        ok = child.Send("{\"id\":" + std::to_string(id) +
                        ",\"method\":\"account/rateLimits/read\"}\n") &&
             child.Reply(id++, stop, result);
        Snapshot s;
        if (ok)
          s = Parse(result);
        {
          std::lock_guard lock(mutex);
          value = s;
        }
        for (int i = 0; i < 600 && ok && !stop.stop_requested(); ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      {
        std::lock_guard lock(mutex);
        value = {};
      }
      for (int i = 0; i < 300 && !stop.stop_requested(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (...) {
    std::lock_guard lock(mutex);
    value = {};
  }
}
}  // namespace jonsbo
