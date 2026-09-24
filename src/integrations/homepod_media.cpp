// SPDX-License-Identifier: GPL-3.0-only
// Additional permission: see LICENSE-SDK-EXCEPTION.
#include "integrations/homepod_media.hpp"

namespace jonsbo {
namespace {
std::wstring Trim(std::wstring text) {
  const auto first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos)
    return {};
  return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

std::wstring Utf8(const std::string& text) {
  if (text.empty())
    return {};
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()),
                                   nullptr, 0);
  if (!length)
    return {};
  std::wstring value(length, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), value.data(),
                      length);
  return value;
}

std::wstring OnPath(const wchar_t* executable) {
  DWORD pathLength = GetEnvironmentVariableW(L"PATH", nullptr, 0);
  if (!pathLength)
    return {};
  std::wstring searchPath(pathLength, L'\0');
  if (!GetEnvironmentVariableW(L"PATH", searchPath.data(), pathLength))
    return {};
  searchPath.resize(pathLength - 1);
  DWORD length = SearchPathW(searchPath.c_str(), executable, nullptr, 0, nullptr, nullptr);
  if (!length)
    return {};
  std::wstring path(length, L'\0');
  DWORD copied = SearchPathW(searchPath.c_str(), executable, nullptr, length, path.data(), nullptr);
  if (!copied || copied >= length)
    return {};
  path.resize(copied);
  return path;
}

std::wstring Quote(const std::wstring& arg) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++slashes;
    } else {
      if (ch == L'"')
        result.append(slashes * 2 + 1, L'\\');
      else
        result.append(slashes, L'\\');
      slashes = 0;
      result += ch;
    }
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}

struct CommandResult {
  DWORD exitCode = 1;
  std::wstring output;
};

CommandResult RunCommand(const std::wstring& executable, const std::vector<std::wstring>& args,
                         std::stop_token stop, DWORD timeoutMs = 60000) {
  CommandResult result;
  if (executable.empty() || stop.stop_requested())
    return result;
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE read = nullptr, write = nullptr;
  if (!CreatePipe(&read, &write, &security, 0))
    return result;
  SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW startup{sizeof(startup)};
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write;
  startup.hStdError = write;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  std::wstring command = Quote(executable);
  for (const auto& arg : args)
    command += L" " + Quote(arg);

  // Python otherwise uses the Windows pipe code page, losing non-ASCII song names.
  std::vector<std::wstring> variables;
  auto environment = GetEnvironmentStringsW();
  if (environment) {
    for (const wchar_t* current = environment; *current; current += wcslen(current) + 1)
      if (_wcsnicmp(current, L"PYTHONIOENCODING=", 17) != 0)
        variables.emplace_back(current);
    FreeEnvironmentStringsW(environment);
  }
  variables.emplace_back(L"PYTHONIOENCODING=utf-8");
  std::sort(variables.begin(), variables.end(), [](const auto& a, const auto& b) {
    return _wcsicmp(a.c_str(), b.c_str()) < 0;
  });
  std::vector<wchar_t> block;
  for (const auto& variable : variables) {
    block.insert(block.end(), variable.begin(), variable.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  PROCESS_INFORMATION process{};
  bool started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.data(),
                                nullptr, &startup, &process) != 0;
  CloseHandle(write);
  if (!started) {
    CloseHandle(read);
    return result;
  }
  std::string bytes;
  auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  while (Clock::now() < deadline && !stop.stop_requested()) {
    DWORD available = 0;
    if (PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr) && available) {
      char buffer[4096];
      DWORD received = 0;
      if (ReadFile(read, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr))
        bytes.append(buffer, received);
      if (bytes.size() > 1024 * 1024)
        break;
    } else if (WaitForSingleObject(process.hProcess, 25) == WAIT_OBJECT_0) {
      break;
    }
  }
  if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0)
    TerminateProcess(process.hProcess, 1);
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr) || !available)
      break;
    char buffer[4096];
    DWORD received = 0;
    if (!ReadFile(read, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) ||
        !received)
      break;
    bytes.append(buffer, received);
    if (bytes.size() > 1024 * 1024)
      break;
  }
  GetExitCodeProcess(process.hProcess, &result.exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(read);
  result.output = Utf8(bytes);
  return result;
}

std::vector<std::wstring> HomePodIds(const std::wstring& scan) {
  std::vector<std::wstring> ids;
  std::wistringstream lines(scan);
  std::wstring line, id;
  bool homepod = false;
  while (std::getline(lines, line)) {
    line = Trim(line);
    if (line.starts_with(L"Name:")) {
      if (homepod && !id.empty())
        ids.push_back(id);
      homepod = false;
      id.clear();
    } else if (line.starts_with(L"Model/SW:")) {
      homepod = line.find(L"HomePod") != std::wstring::npos;
    } else if (line.starts_with(L"- ") && id.empty()) {
      auto candidate = Trim(line.substr(2));
      if (candidate.size() == 17 &&
          candidate.find_first_not_of(L"0123456789abcdefABCDEF:") == std::wstring::npos)
        id = candidate;
    }
  }
  if (homepod && !id.empty())
    ids.push_back(id);
  return ids;
}

MediaState ParsePlaying(const std::wstring& output, const std::wstring& id) {
  MediaState media;
  media.title.clear();
  media.artist.clear();
  std::wstring status;
  std::wistringstream lines(output);
  std::wstring line;
  while (std::getline(lines, line)) {
    line = Trim(line);
    auto colon = line.find(L':');
    if (colon == std::wstring::npos)
      continue;
    auto key = Trim(line.substr(0, colon));
    auto value = Trim(line.substr(colon + 1));
    if (key == L"Device state")
      status = value;
    else if (key == L"Title")
      media.title = value;
    else if (key == L"Artist")
      media.artist = value;
    else if (key == L"Position") {
      auto slash = value.find(L'/');
      auto seconds = value.find(L's', slash);
      if (slash != std::wstring::npos && seconds != std::wstring::npos) {
        try {
          media.position = std::stod(value.substr(0, slash));
          media.duration = std::stod(value.substr(slash + 1, seconds - slash - 1));
        } catch (...) {
        }
      }
    }
  }
  media.available = !media.title.empty() && status == L"Playing";
  media.playing = media.available;
  media.homepod = media.available;
  media.identity = id;
  media.sampled = Clock::now();
  if (media.artist.empty())
    media.artist = L"未知艺术家";
  if (!media.available)
    return {};
  return media;
}

std::shared_ptr<std::vector<uint8_t>> ReadArtwork(const std::filesystem::path& path) {
  std::error_code error;
  auto size = std::filesystem::file_size(path, error);
  if (error || size == 0 || size > 16 * 1024 * 1024)
    return {};
  auto bytes = std::make_shared<std::vector<uint8_t>>(size);
  std::ifstream file(path, std::ios::binary);
  if (!file.read(reinterpret_cast<char*>(bytes->data()), std::streamsize(size)))
    return {};
  return bytes;
}
}  // namespace

HomePodMedia::HomePodMedia() : worker([this](std::stop_token stop) { Run(stop); }) {}

HomePodMedia::~HomePodMedia() {
  worker.request_stop();
  wake.notify_all();
}

void HomePodMedia::SetActive(bool value) {
  std::lock_guard lock(mutex);
  if (active == value)
    return;
  active = value;
  refresh = value;
  if (!value)
    state = {};
  wake.notify_all();
}

MediaState HomePodMedia::Read() {
  std::lock_guard lock(mutex);
  return state;
}

void HomePodMedia::Run(std::stop_token stop) {
  std::wstring executable = OnPath(L"atvremote.exe");
  std::vector<std::wstring> prefix;
  if (executable.empty()) {
    executable = OnPath(L"uvx.exe");
    prefix = {L"--python", L"3.13", L"--from", L"pyatv", L"atvremote"};
  }
  std::vector<std::wstring> ids;
  auto scanned = Clock::time_point{};
  std::wstring artworkKey;
  std::shared_ptr<std::vector<uint8_t>> artwork;
  while (!stop.stop_requested()) {
    {
      std::unique_lock lock(mutex);
      wake.wait_for(lock, std::chrono::seconds(2), [&] {
        return stop.stop_requested() || refresh || !active;
      });
      if (stop.stop_requested())
        break;
      if (!active) {
        wake.wait(lock, [&] { return stop.stop_requested() || active; });
        continue;
      }
      if (refresh) {
        ids.clear();
        artworkKey.clear();
        artwork.reset();
        refresh = false;
      }
    }
    if (executable.empty())
      continue;
    if (ids.empty() || Clock::now() - scanned > std::chrono::seconds(30)) {
      scanned = Clock::now();
      auto args = prefix;
      args.push_back(L"scan");
      ids = HomePodIds(RunCommand(executable, args, stop).output);
    }
    MediaState latest;
    for (const auto& id : ids) {
      auto args = prefix;
      args.insert(args.end(), {L"--id", id, L"playing"});
      auto answer = RunCommand(executable, args, stop);
      if (answer.exitCode != 0)
        continue;
      latest = ParsePlaying(answer.output, id);
      if (latest.available)
        break;
    }
    if (latest.available) {
      std::wstring key = latest.identity + L"|" + latest.title + L"|" + latest.artist;
      if (key != artworkKey) {
        artworkKey = key;
        artwork.reset();
        auto file = std::filesystem::temp_directory_path() /
                    (L"jonsbo-homepod-artwork-" + std::to_wstring(GetCurrentProcessId()) +
                     L".png");
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        auto args = prefix;
        args.insert(args.end(), {L"--id", latest.identity,
                                 L"artwork_save=512,512," + file.wstring().substr(0, file.wstring().size() - 4)});
        if (RunCommand(executable, args, stop).exitCode == 0)
          artwork = ReadArtwork(file);
        std::filesystem::remove(file, ignored);
      }
      latest.artwork = artwork;
      latest.revision = std::hash<std::wstring>{}(artworkKey);
    } else {
      artworkKey.clear();
      artwork.reset();
    }
    std::lock_guard lock(mutex);
    if (active)
      state = std::move(latest);
  }
}
}  // namespace jonsbo
