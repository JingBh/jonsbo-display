// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "core/utilities.hpp"

namespace jonsbo {
std::filesystem::path ExeDir() {
  wchar_t p[32768];
  GetModuleFileNameW(nullptr, p, 32768);
  return std::filesystem::path(p).parent_path();
}
std::wstring Ini(const std::filesystem::path& p, const wchar_t* section, const wchar_t* key,
                 const wchar_t* def) {
  wchar_t b[1024];
  GetPrivateProfileStringW(section, key, def, b, 1024, p.c_str());
  return b;
}
std::wstring Fixed(double n, int digits) {
  std::wostringstream s;
  s << std::fixed << std::setprecision(digits) << n;
  return s.str();
}
std::wstring BitRate(double bytesPerSecond) {
  double bits = std::max(0., bytesPerSecond) * 8;
  return bits >= 1e6   ? Fixed(bits / 1e6, 1) + L" Mbps"
         : bits >= 1e3 ? Fixed(bits / 1e3, 1) + L" Kbps"
                       : Fixed(bits) + L" bps";
}

void Check(HRESULT hr) {
  if (FAILED(hr)) {
    std::ostringstream s;
    s << "Windows API error 0x" << std::hex << hr;
    throw std::runtime_error(s.str());
  }
}
float Seconds(Clock::time_point time) {
  return std::chrono::duration<float>(Clock::now() - time).count();
}
double ClockSeconds() {
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
std::string ToUtf8(const std::wstring& value) {
  if (value.empty())
    return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                        nullptr, 0, nullptr, nullptr);
  std::string output(bytes, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(),
                      bytes, nullptr, nullptr);
  return output;
}

}  // namespace jonsbo
