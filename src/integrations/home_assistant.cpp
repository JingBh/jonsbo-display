// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "integrations/home_assistant.hpp"
namespace jonsbo {
std::wstring CurtainLabel(double position) {
  return position <= 10 ? L"关" : position >= 90 ? L"开" : position >= 50 ? L"半开" : L"半关";
}
double ColorLuminance(unsigned color) {
  double result = 0;
  const double weights[] = {.2126, .7152, .0722};
  int i = 0;
  for (int shift : {16, 8, 0}) {
    double c = ((color >> shift) & 255) / 255.;
    result += weights[i++] * (c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4));
  }
  return result;
}
// Conservative lightest HA card background; lift only colors below 4.5:1.
unsigned ReadableLightColor(unsigned color) {
  constexpr unsigned background = 0x41415b;
  double target = 4.5 * (ColorLuminance(background) + .05) - .05;
  if (ColorLuminance(color) >= target)
    return color;
  auto mix = [&](double t) {
    unsigned result = 0;
    for (int shift : {16, 8, 0}) {
      double c = (color >> shift) & 255;
      result |= unsigned(std::ceil(c + (255 - c) * t)) << shift;
    }
    return result;
  };
  double lo = 0, hi = 1;
  for (int i = 0; i < 16; ++i) {
    double mid = (lo + hi) / 2;
    if (ColorLuminance(mix(mid)) >= target)
      hi = mid;
    else
      lo = mid;
  }
  return mix(hi);
}
unsigned LightColor(const winrt::Windows::Data::Json::JsonObject& o) {
  using Type = winrt::Windows::Data::Json::JsonValueType;
  auto rgb = o.GetNamedValue(L"rgb_color", nullptr);
  if (rgb && rgb.ValueType() == Type::Array) {
    auto a = rgb.GetArray();
    if (a.Size() == 3) {
      unsigned color = 0;
      bool valid = true;
      for (unsigned i = 0; i < 3; ++i) {
        auto v = a.GetAt(i);
        if (v.ValueType() != Type::Number || !std::isfinite(v.GetNumber())) {
          valid = false;
          break;
        }
        color = (color << 8) | unsigned(std::clamp(std::round(v.GetNumber()), 0., 255.));
      }
      if (valid)
        return color;
    }
  }
  // Fallback warm/cool palette when HA does not supply its derived RGB value.
  auto kelvin = o.GetNamedValue(L"color_temp_kelvin", nullptr);
  if (kelvin && kelvin.ValueType() == Type::Number) {
    double k = kelvin.GetNumber();
    if (std::isfinite(k) && k > 0) {
      constexpr double temperatures[] = {2000, 2700, 4000, 6500, 10000};
      constexpr unsigned colors[] = {0xff8912, 0xffa757, 0xffcea6, 0xfffefa, 0xcadaff};
      k = std::clamp(k, 2000., 10000.);
      int i = 0;
      while (i < 3 && k > temperatures[i + 1])
        ++i;
      double t = (k - temperatures[i]) / (temperatures[i + 1] - temperatures[i]);
      unsigned result = 0;
      for (int shift : {16, 8, 0})
        result |= unsigned(std::round(((colors[i] >> shift) & 255) * (1 - t) +
                                      ((colors[i + 1] >> shift) & 255) * t))
                  << shift;
      return result;
    }
  }
  return 0xe6cfae;
}
HomeState HomeAssistant::Read() {
  std::lock_guard lock(mutex);
  return state;
}

std::wstring HomeAssistant::CredentialTarget(const std::wstring& host) {
  return L"JonsboDisplay/HomeAssistant/" + host;
}

bool HomeAssistant::SaveToken(const std::wstring& host, std::string token) {
  if (token.empty() || token.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
    return false;
  std::wstring target = CredentialTarget(host);
  CREDENTIALW c{};
  c.Type = CRED_TYPE_GENERIC;
  c.TargetName = target.data();
  c.CredentialBlobSize = DWORD(token.size());
  c.CredentialBlob = reinterpret_cast<BYTE*>(token.data());
  c.Persist = CRED_PERSIST_LOCAL_MACHINE;
  c.UserName = const_cast<wchar_t*>(L"Home Assistant");
  bool ok = CredWriteW(&c, 0) != FALSE;
  SecureZeroMemory(token.data(), token.size());
  return ok;
}

double HomeAssistant::Number(const Json& o, const wchar_t* key, double fallback) {
  auto v = o.GetNamedValue(key, nullptr);
  return v && v.ValueType() == winrt::Windows::Data::Json::JsonValueType::Number ? v.GetNumber()
                                                                                 : fallback;
}

std::string HomeAssistant::Request(const std::wstring& host, INTERNET_PORT port,
                                   const std::wstring& path, const std::string& body) {
  PCREDENTIALW credential = nullptr;
  if (!CredReadW(CredentialTarget(host).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    const std::wstring legacyTarget = L"Display480/HomeAssistant/" + host;
    if (!CredReadW(legacyTarget.c_str(), CRED_TYPE_GENERIC, 0, &credential))
      throw std::runtime_error("未配置令牌");
  }
  std::string token(reinterpret_cast<char*>(credential->CredentialBlob),
                    credential->CredentialBlobSize);
  CredFree(credential);
  std::wstring headers = std::wstring(L"Content-Type: application/json\r\nAuthorization: Bearer ") +
                         winrt::to_hstring(token).c_str();
  SecureZeroMemory(token.data(), token.size());
  Internet session(WinHttpOpen(L"JonsboDisplay/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session)
    throw std::runtime_error("连接不可用");
  WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);
  Internet connection(WinHttpConnect(session, host.c_str(), port, 0));
  Internet request(WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE));
  if (!request)
    throw std::runtime_error("连接不可用");
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
  bool sent = WinHttpSendRequest(request, headers.c_str(), DWORD(headers.size()),
                                 const_cast<char*>(body.data()), DWORD(body.size()),
                                 DWORD(body.size()), 0) != FALSE;
  SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
  if (!sent || !WinHttpReceiveResponse(request, nullptr))
    throw std::runtime_error("连接中断");
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                      &status, &size, nullptr);
  if (status != 200)
    throw std::runtime_error(status == 401 ? "令牌失效" : "服务暂不可用");
  std::string response;
  DWORD count = 0;
  while (WinHttpQueryDataAvailable(request, &count) && count) {
    if (response.size() + count > 65536)
      throw std::runtime_error("响应过大");
    size_t offset = response.size();
    response.resize(offset + count);
    DWORD read = 0;
    if (!WinHttpReadData(request, response.data() + offset, count, &read))
      throw std::runtime_error("读取失败");
    response.resize(offset + read);
  }
  return response;
}

void HomeAssistant::Run(std::stop_token stop) {
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    std::wstring url = Ini(config, L"home_assistant", L"url");
    if (url.empty())
      return;
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS)
      throw std::runtime_error("需要 HTTPS 地址");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
        path(parts.lpszUrlPath, parts.dwUrlPathLength);
    while (!path.empty() && path.back() == L'/')
      path.pop_back();
    path += L"/api/template";
    const wchar_t* keys[] = {L"temperature", L"humidity",        L"ceiling",
                             L"spot",        L"strip",           L"curtain",
                             L"sheer",       L"air_conditioner", L"floor_heating"};
    std::string ids = "[";
    for (int i = 0; i < 9; ++i) {
      auto id = Ini(config, L"home_assistant", keys[i]);
      if (id.empty() ||
          id.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789_.") != std::wstring::npos)
        throw std::runtime_error("实体配置不完整");
      if (i)
        ids += ",";
      ids += "'" + ToUtf8(id) + "'";
    }
    ids += "]";
    // Template rendering reads exactly the selected entities; no service/state writes.
    std::string source =
        "{% set ns=namespace(items=[]) %}{% for id in " + ids +
        " %}{% set "
        "ns.items=ns.items+[dict(state=states(id),brightness=state_attr(id,'brightness'),rgb_color="
        "state_attr(id,'rgb_color'),color_temp_kelvin=state_attr(id,'color_temp_kelvin'),current_"
        "position=state_attr(id,'current_position'),temperature=state_attr(id,'temperature'))] "
        "%}{% endfor %}{{ ns.items | to_json }}";
    Json payload;
    payload.Insert(L"template", winrt::Windows::Data::Json::JsonValue::CreateStringValue(
                                    winrt::to_hstring(source)));
    std::string body = winrt::to_string(payload.Stringify());
    while (!stop.stop_requested()) {
      HomeState next;
      try {
        auto response = Request(host, parts.nPort, path, body);
        auto array = winrt::Windows::Data::Json::JsonArray::Parse(winrt::to_hstring(response));
        if (array.Size() != 9)
          throw std::runtime_error("实体响应异常");
        auto get = [&](int i) { return array.GetObjectAt(i); };
        auto available = [](const std::wstring& s) {
          return s != L"unknown" && s != L"unavailable" && s != L"none";
        };
        auto sensor = [&](int index, int decimals) {
          auto raw = get(index).GetNamedString(L"state", L"unknown");
          try {
            size_t n = 0;
            double value = std::stod(raw.c_str(), &n);
            if (n == raw.size() && std::isfinite(value))
              return Fixed(value, decimals);
          } catch (...) {
          }
          return std::wstring(L"—");
        };
        next.temperature = sensor(0, 1);
        next.humidity = sensor(1, 0);
        for (int i = 0; i < 3; ++i) {
          auto o = get(i + 2);
          std::wstring s = o.GetNamedString(L"state").c_str();
          auto& d = next.lights[i];
          d.available = available(s);
          d.active = s == L"on";
          d.color = LightColor(o);
          double brightness = Number(o, L"brightness");
          d.value = !d.available      ? L"—"
                    : !d.active       ? L"关"
                    : brightness >= 0 ? Fixed(std::clamp(brightness / 255 * 100, 0., 100.)) + L"%"
                                      : L"开";
        }
        for (int i = 0; i < 2; ++i) {
          auto o = get(i + 5);
          std::wstring s = o.GetNamedString(L"state").c_str();
          auto& d = next.covers[i];
          d.available = available(s);
          double position = Number(o, L"current_position");
          d.active = d.available && (position >= 0 ? position > 10 : s != L"closed");
          d.value = !d.available      ? L"—"
                    : position >= 0   ? CurtainLabel(position)
                    : s == L"closed"  ? L"关"
                    : s == L"open"    ? L"开"
                    : s == L"opening" ? L"开启中"
                    : s == L"closing" ? L"关闭中"
                                      : L"—";
        }
        for (int i = 0; i < 2; ++i) {
          auto o = get(i + 7);
          std::wstring s = o.GetNamedString(L"state").c_str();
          auto& d = next.climate[i];
          d.available = available(s);
          d.active = d.available && s != L"off";
          double temperature = Number(o, L"temperature");
          std::wstring mode = s == L"cool"       ? L"制冷"
                              : s == L"heat"     ? L"制热"
                              : s == L"dry"      ? L"除湿"
                              : s == L"fan_only" ? L"送风"
                                                 : L"自动";
          d.value = !d.available ? L"—"
                    : !d.active
                        ? L"关"
                        : mode + (temperature >= 0 ? L" " + Fixed(temperature) + L"°" : L"");
        }
        next.connected = true;
        next.status = L"";
      } catch (const std::exception&) {
        next.status = L"暂时离线";
      }
      {
        std::lock_guard lock(mutex);
        state = next;
      }
      for (int i = 0; i < 50 && !stop.stop_requested(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (...) {
    std::lock_guard lock(mutex);
    state.status = L"配置不可用";
  }
}
}  // namespace jonsbo
