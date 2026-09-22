// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "device/display_transport.hpp"

namespace jonsbo {
void DisplayTransport::Start() {
  worker = std::jthread([this](std::stop_token stop) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32W entry{sizeof(entry)};
      bool conflict = false;
      if (Process32FirstW(snapshot, &entry))
        do {
          if (_wcsicmp(entry.szExeFile, L"JONSBO-AIO.exe") == 0)
            conflict = true;
        } while (Process32NextW(snapshot, &entry));
      CloseHandle(snapshot);
      if (conflict) {
        std::lock_guard lock(mutex);
        status = L"Exit JONSBO-AIO before USB output";
        return;
      }
    }
    JonsboDisplaySession session;
    std::wstring error;
    if (!session.Open(-1, &error)) {
      std::lock_guard lock(mutex);
      status = error;
      return;
    }
    Frame local;
    uint64_t sent = 0;
    auto next = Clock::now();
    while (!stop.stop_requested()) {
      bool changed = false;
      {
        std::lock_guard lock(mutex);
        if (version != sent) {
          std::memcpy(local.data(), latest.data(), 480 * 480 * 4);
          sent = version;
          changed = true;
        }
      }
      int result = 0;
      if (changed) {
        if (!session.Send(local, true, &result, &error)) {
          std::lock_guard lock(mutex);
          status = error.empty() ? L"USB send failed" : error;
          break;
        }
      } else if (sent)
        result = session.RepeatPreparedFrame();
      if (sent) {
        if (result == 0)
          ++accepted;
        else
          ++rejected;
      }
      next += std::chrono::microseconds(16667);
      if (next < Clock::now())
        next = Clock::now();
      std::this_thread::sleep_until(next);
    }
  });
}

void DisplayTransport::Restart() {
  worker.request_stop();
  if (worker.joinable())
    worker.join();
  {
    std::lock_guard lock(mutex);
    status.clear();
  }
  Start();
}

void DisplayTransport::Submit(const Frame& frame) {
  std::lock_guard lock(mutex);
  std::memcpy(latest.data(), frame.data(), 480 * 480 * 4);
  ++version;
}

std::wstring DisplayTransport::Status() {
  std::lock_guard lock(mutex);
  return status;
}
}  // namespace jonsbo
