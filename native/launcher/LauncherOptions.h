#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace PrismForge::Launcher {

struct Options {
  std::optional<std::filesystem::path> engine;
  std::optional<std::filesystem::path> control;
  bool checkLayout = false;
  bool engineOnly = false;
  bool launchpad = false;
  bool legacyRecursiveAudio = false;
  bool help = false;
};

std::optional<Options> ParseOptions(int argc, wchar_t* const* argv);

std::wstring BuildProcessCommandLine(const std::filesystem::path& executable,
                                     bool engine,
                                     bool launchpad,
                                     bool legacyRecursiveAudio);

}  // namespace PrismForge::Launcher
