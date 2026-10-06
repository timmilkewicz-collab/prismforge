#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include "LauncherOptions.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {
constexpr wchar_t kEngineMutex[] = L"Local\\PrismForge.Engine";
constexpr wchar_t kLauncherMutex[] = L"Local\\PrismForge.Launcher";

class Handle {
 public:
  explicit Handle(HANDLE value = nullptr) : value_(value) {}
  ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) {
      if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
      value_ = other.value_;
      other.value_ = nullptr;
    }
    return *this;
  }
  HANDLE Get() const { return value_; }
  explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }

 private:
  HANDLE value_;
};

using PrismForge::Launcher::BuildProcessCommandLine;
using PrismForge::Launcher::Options;
using PrismForge::Launcher::ParseOptions;

std::wstring SystemError(DWORD error) {
  wchar_t* text = nullptr;
  const DWORD count = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
  std::wstring result = count && text ? std::wstring(text, count) : L"Windows error";
  if (text) LocalFree(text);
  while (!result.empty() && std::iswspace(result.back())) result.pop_back();
  return result + L" (" + std::to_wstring(error) + L")";
}

void ShowError(const std::wstring& message) {
  MessageBoxW(nullptr, message.c_str(), L"PrismForge Launcher", MB_OK | MB_ICONERROR);
}

std::filesystem::path ExecutableDirectory() {
  std::wstring buffer(32768, L'\0');
  const DWORD count = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
  if (!count || count >= buffer.size()) return {};
  buffer.resize(count);
  return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path FullPath(const std::filesystem::path& path) {
  std::error_code error;
  auto result = std::filesystem::weakly_canonical(path, error);
  if (!error) return result;
  error.clear();
  result = std::filesystem::absolute(path, error);
  return error ? path : result;
}

bool SameFile(const std::filesystem::path& left,
              const std::filesystem::path& right) {
  std::error_code error;
  if (std::filesystem::equivalent(left, right, error)) return true;
  std::wstring a = FullPath(left).wstring();
  std::wstring b = FullPath(right).wstring();
  std::transform(a.begin(), a.end(), a.begin(), std::towlower);
  std::transform(b.begin(), b.end(), b.begin(), std::towlower);
  return a == b;
}

bool ExistingFile(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

DWORD FindProcessByPath(const std::filesystem::path& expected) {
  Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot) return 0;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot.Get(), &entry)) return 0;
  do {
    if (entry.th32ProcessID == GetCurrentProcessId()) continue;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               entry.th32ProcessID));
    if (!process) continue;
    std::wstring image(32768, L'\0');
    DWORD count = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(process.Get(), 0, image.data(), &count)) continue;
    image.resize(count);
    if (SameFile(expected, image)) return entry.th32ProcessID;
  } while (Process32NextW(snapshot.Get(), &entry));
  return 0;
}

struct WindowSearch {
  DWORD processId;
  HWND window = nullptr;
};

BOOL CALLBACK FindMainWindow(HWND window, LPARAM parameter) {
  auto& search = *reinterpret_cast<WindowSearch*>(parameter);
  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  if (processId == search.processId && IsWindowVisible(window) &&
      GetWindow(window, GW_OWNER) == nullptr) {
    search.window = window;
    return FALSE;
  }
  return TRUE;
}

void ActivateControl(DWORD processId) {
  WindowSearch search{processId};
  EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&search));
  if (!search.window) return;
  ShowWindow(search.window, SW_RESTORE);
  if (!SetForegroundWindow(search.window)) {
    FLASHWINFO flash{sizeof(flash), search.window, FLASHW_TRAY | FLASHW_TIMERNOFG,
                     3, 0};
    FlashWindowEx(&flash);
  }
}

std::optional<std::filesystem::path> EngineLogPath() {
  const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  if (required <= 1) return std::nullopt;
  std::wstring value(required, L'\0');
  const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), required);
  if (!count || count >= required) return std::nullopt;
  value.resize(count);
  const auto directory = std::filesystem::path(value) / "PrismForge" / "Logs";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) return std::nullopt;
  return directory / "engine.log";
}

bool StartProcess(const std::filesystem::path& executable, bool engine,
                  bool launchpad, bool legacyRecursiveAudio,
                  DWORD& processId, std::wstring& error) {
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  Handle log;
  Handle input;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  DWORD flags = 0;
  BOOL inheritHandles = FALSE;
  if (engine) {
    const auto logPath = EngineLogPath();
    if (!logPath) {
      error = L"Cannot create %LOCALAPPDATA%\\PrismForge\\Logs.";
      return false;
    }
    log = Handle(CreateFileW(logPath->c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!log) {
      error = L"Cannot open the Engine log: " + SystemError(GetLastError());
      return false;
    }
    input = Handle(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!input) {
      error = L"Cannot open null Engine input: " + SystemError(GetLastError());
      return false;
    }
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.Get();
    startup.hStdOutput = log.Get();
    startup.hStdError = log.Get();
    inheritHandles = TRUE;
    flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
  }

  const std::wstring path = executable.wstring();
  const std::wstring directory = executable.parent_path().wstring();
  std::wstring command = BuildProcessCommandLine(
      executable, engine, launchpad, legacyRecursiveAudio);
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(path.c_str(), mutableCommand.data(), nullptr, nullptr,
                      inheritHandles, flags, nullptr, directory.c_str(),
                      &startup, &process)) {
    error = L"Could not start " + path + L": " + SystemError(GetLastError());
    return false;
  }
  processId = process.dwProcessId;
  if (engine && WaitForSingleObject(process.hProcess, 1000) == WAIT_OBJECT_0) {
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    error = L"Engine exited during startup (code " + std::to_wstring(exitCode) +
            L"). Check %LOCALAPPDATA%\\PrismForge\\Logs\\engine.log.";
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

int Run(const Options& options) {
  const auto executableDirectory = ExecutableDirectory();
  if (executableDirectory.empty()) {
    if (!options.checkLayout) ShowError(L"Cannot resolve the Launcher executable path.");
    return 3;
  }
  const auto engine = FullPath(options.engine.value_or(
      executableDirectory / "PrismForge.Engine.exe"));
  const auto control = FullPath(options.control.value_or(
      executableDirectory / "PrismForge.Control.exe"));

  if (options.checkLayout) {
    return ExistingFile(engine) && (options.engineOnly || ExistingFile(control)) ? 0 : 3;
  }
  if (!options.engineOnly && !ExistingFile(control)) {
    ShowError(L"Control executable is missing:\n" + control.wstring() +
              L"\n\nPlace the portable executables together or pass --control.");
    return 3;
  }

  Handle launchMutex(CreateMutexW(nullptr, FALSE, kLauncherMutex));
  if (!launchMutex) {
    ShowError(L"Cannot acquire Launcher coordination mutex: " +
              SystemError(GetLastError()));
    return 4;
  }
  const DWORD waited = WaitForSingleObject(launchMutex.Get(), 10000);
  if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
    ShowError(L"Another Launcher is still starting PrismForge. Try again shortly.");
    return 4;
  }
  int result = 0;
  Handle engineMutex(OpenMutexW(SYNCHRONIZE, FALSE, kEngineMutex));
  const DWORD mutexError = engineMutex ? ERROR_SUCCESS : GetLastError();
  if (engineMutex) {
    if (!FindProcessByPath(engine)) {
      ShowError(L"A PrismForge Engine is already running from another location or "
                L"cannot be inspected. This Launcher will not replace it or "
                L"connect Control to an unverified build.\n\nExpected:\n" +
                engine.wstring());
      result = 4;
    } else if (options.launchpad && !options.legacyRecursiveAudio) {
      ShowError(L"The Engine is already running. --launchpad only applies when "
                L"starting a new Engine; this Launcher will not restart healthy output.");
      result = 4;
    } else if (options.legacyRecursiveAudio) {
      ShowError(L"The Engine is already running. --legacy-recursive-audio only "
                L"applies when starting a new Engine; this Launcher will not "
                L"restart healthy output.");
      result = 4;
    }
  } else if (mutexError != ERROR_FILE_NOT_FOUND) {
    ShowError(L"Cannot determine whether the Engine is running: " +
              SystemError(mutexError));
    result = 4;
  } else if (!ExistingFile(engine)) {
    ShowError(L"Engine executable is missing:\n" + engine.wstring() +
              L"\n\nPlace the portable executables together or pass --engine.");
    result = 3;
  } else {
    DWORD processId = 0;
    std::wstring error;
    if (!StartProcess(engine, true, options.launchpad,
                      options.legacyRecursiveAudio, processId, error)) {
      ShowError(error);
      result = 5;
    }
  }

  if (result == 0 && !options.engineOnly) {
    const DWORD controlProcessId = FindProcessByPath(control);
    if (controlProcessId) {
      ActivateControl(controlProcessId);
    } else {
      DWORD processId = 0;
      std::wstring error;
      if (!StartProcess(control, false, false, false, processId, error)) {
        ShowError(error + L"\n\nThe Engine was not stopped; relaunch Control after fixing this.");
        result = 6;
      }
    }
  }
  ReleaseMutex(launchMutex.Get());
  return result;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    ShowError(L"Cannot parse Launcher command line.");
    return 64;
  }
  const auto options = ParseOptions(argc, argv);
  LocalFree(argv);
  if (!options) {
    ShowError(L"Usage: PrismForge.Launcher [--engine PATH] [--control PATH] "
              L"[--engine-only] [--launchpad] [--legacy-recursive-audio] "
              L"[--check-layout]");
    return 64;
  }
  if (options->help) {
    MessageBoxW(nullptr,
                L"PrismForge.Launcher [--engine PATH] [--control PATH] "
                L"[--engine-only] [--launchpad] [--legacy-recursive-audio] "
                L"[--check-layout]",
                L"PrismForge Launcher", MB_OK | MB_ICONINFORMATION);
    return 0;
  }
  return Run(*options);
}
