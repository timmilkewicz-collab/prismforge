#include "LauncherOptions.h"

namespace PrismForge::Launcher {

std::optional<Options> ParseOptions(int argc, wchar_t* const* argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::wstring argument = argv[i];
    if (argument == L"--engine" && i + 1 < argc) {
      options.engine = argv[++i];
    } else if (argument == L"--control" && i + 1 < argc) {
      options.control = argv[++i];
    } else if (argument == L"--check-layout") {
      options.checkLayout = true;
    } else if (argument == L"--engine-only") {
      options.engineOnly = true;
    } else if (argument == L"--launchpad") {
      options.launchpad = true;
    } else if (argument == L"--legacy-recursive-audio") {
      options.legacyRecursiveAudio = true;
    } else if (argument == L"--help") {
      options.help = true;
    } else {
      return std::nullopt;
    }
  }
  return options;
}

std::wstring BuildProcessCommandLine(const std::filesystem::path& executable,
                                     bool engine,
                                     bool launchpad,
                                     bool legacyRecursiveAudio) {
  std::wstring command = L"\"" + executable.wstring() + L"\"";
  if (!engine) return command;
  if (launchpad) command += L" --launchpad";
  if (legacyRecursiveAudio) command += L" --legacy-recursive-audio";
  return command;
}

}  // namespace PrismForge::Launcher
