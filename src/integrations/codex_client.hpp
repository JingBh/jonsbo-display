// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#pragma once
#include "core/utilities.hpp"
#include <winrt/Windows.Data.Json.h>

namespace jonsbo {
// Read-only JSON-RPC adapter. No threads/turns, login mutation or reset calls.
class CodexLimits {
 public:
  struct Window {
    double used = 0;
    int minutes = 0;
    int64_t resetsAt = 0;
  };
  struct Snapshot {
    std::vector<Window> windows;
  };
  static Snapshot Parse(const winrt::Windows::Data::Json::JsonObject& result);
  explicit CodexLimits(std::wstring executable)
      : worker([this, executable](std::stop_token stop) { Run(stop, executable); }) {}
  Snapshot Read();

 private:
  struct PipeProcess {
    HANDLE input = nullptr, output = nullptr, process = nullptr;
    ~PipeProcess() {
      if (input)
        CloseHandle(input);
      if (process) {
        if (WaitForSingleObject(process, 500) == WAIT_TIMEOUT)
          TerminateProcess(process, 0);
        CloseHandle(process);
      }
      if (output)
        CloseHandle(output);
    }
    bool Start(const std::wstring& exe) {
      SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
      HANDLE childIn = nullptr, childOut = nullptr;
      if (!CreatePipe(&childIn, &input, &sa, 0))
        return false;
      if (!CreatePipe(&output, &childOut, &sa, 0)) {
        CloseHandle(childIn);
        return false;
      }
      SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
      SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
      HANDLE null = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                OPEN_EXISTING, 0, nullptr);
      STARTUPINFOW si{sizeof(si)};
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdInput = childIn;
      si.hStdOutput = childOut;
      si.hStdError = null;
      PROCESS_INFORMATION pi{};
      std::wstring args = L"\"" + exe + L"\" app-server --stdio";
      bool ok = CreateProcessW(exe.c_str(), args.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &si, &pi) != FALSE;
      CloseHandle(childIn);
      CloseHandle(childOut);
      if (null != INVALID_HANDLE_VALUE)
        CloseHandle(null);
      if (ok) {
        process = pi.hProcess;
        CloseHandle(pi.hThread);
      }
      return ok;
    }
    bool Send(const std::string& message) {
      DWORD n = 0;
      return WriteFile(input, message.data(), DWORD(message.size()), &n, nullptr) &&
             n == message.size();
    }
    bool Reply(int id, std::stop_token stop, winrt::Windows::Data::Json::JsonObject& result) {
      using namespace winrt::Windows::Data::Json;
      auto deadline = Clock::now() + std::chrono::seconds(15);
      while (!stop.stop_requested() && Clock::now() < deadline) {
        size_t end;
        while ((end = buffer.find('\n')) != std::string::npos) {
          auto line = buffer.substr(0, end);
          buffer.erase(0, end + 1);
          JsonObject o;
          if (JsonObject::TryParse(winrt::to_hstring(line), o) &&
              o.GetNamedNumber(L"id", -1) == id) {
            if (!o.HasKey(L"result"))
              return false;
            result = o.GetNamedObject(L"result");
            return true;
          }
        }
        DWORD available = 0;
        if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr))
          return false;
        if (available) {
          char bytes[8192];
          DWORD n = 0;
          if (!ReadFile(output, bytes, std::min<DWORD>(available, sizeof(bytes)), &n, nullptr))
            return false;
          buffer.append(bytes, n);
          if (buffer.size() > 1024 * 1024)
            return false;
        } else {
          if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
            return false;
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      }
      return false;
    }
    std::string buffer;
  };
  void Run(std::stop_token stop, const std::wstring& executable);
  std::mutex mutex;
  Snapshot value;
  std::jthread worker;
};

}  // namespace jonsbo
