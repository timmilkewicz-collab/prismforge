#include "LauncherOptions.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

using PrismForge::Launcher::BuildProcessCommandLine;
using PrismForge::Launcher::Options;
using PrismForge::Launcher::ParseOptions;

std::optional<Options> Parse(std::vector<std::wstring> arguments) {
  std::vector<wchar_t*> argv;
  argv.reserve(arguments.size());
  for (auto& argument : arguments) argv.push_back(argument.data());
  return ParseOptions(static_cast<int>(argv.size()), argv.data());
}

bool Expect(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << "FAILED: " << message << '\n';
  return false;
}

}  // namespace

int main() {
  bool ok = true;
  const std::filesystem::path engine =
      LR"(C:\PrismForge Candidate\PrismForge.Engine.exe)";
  const std::filesystem::path control =
      LR"(C:\PrismForge Candidate\PrismForge.Control.exe)";

  const auto normal = Parse(
      {L"PrismForge.Launcher.exe", L"--check-layout", L"--engine",
       engine.wstring(), L"--control", control.wstring()});
  ok &= Expect(normal.has_value(), "normal layout arguments should parse");
  if (normal) {
    ok &= Expect(normal->checkLayout, "normal layout should preserve --check-layout");
    ok &= Expect(!normal->legacyRecursiveAudio,
                 "normal launch must keep legacy rollback disabled by default");
    ok &= Expect(BuildProcessCommandLine(engine, true, normal->launchpad,
                                         normal->legacyRecursiveAudio) ==
                     L"\"C:\\PrismForge Candidate\\PrismForge.Engine.exe\"",
                 "normal Engine command must not gain rollback arguments");
  }

  const auto legacy =
      Parse({L"PrismForge.Launcher.exe", L"--legacy-recursive-audio"});
  ok &= Expect(legacy.has_value(), "legacy rollback arguments should parse");
  if (legacy) {
    ok &= Expect(legacy->legacyRecursiveAudio,
                 "legacy rollback option should be retained");
    ok &= Expect(BuildProcessCommandLine(engine, true, legacy->launchpad,
                                         legacy->legacyRecursiveAudio) ==
                     L"\"C:\\PrismForge Candidate\\PrismForge.Engine.exe\" "
                     L"--legacy-recursive-audio",
                 "legacy rollback flag should be forwarded to Engine");
    ok &= Expect(BuildProcessCommandLine(control, false, legacy->launchpad,
                                         legacy->legacyRecursiveAudio) ==
                     L"\"C:\\PrismForge Candidate\\PrismForge.Control.exe\"",
                 "Engine-only rollback flag must not be forwarded to Control");
  }

  const auto combined =
      Parse({L"PrismForge.Launcher.exe", L"--launchpad",
             L"--legacy-recursive-audio"});
  ok &= Expect(combined.has_value(), "supported Engine flags should compose");
  if (combined) {
    ok &= Expect(BuildProcessCommandLine(engine, true, combined->launchpad,
                                         combined->legacyRecursiveAudio) ==
                     L"\"C:\\PrismForge Candidate\\PrismForge.Engine.exe\" "
                     L"--launchpad --legacy-recursive-audio",
                 "Engine flags should each be forwarded exactly once");
  }

  ok &= Expect(!Parse({L"PrismForge.Launcher.exe", L"--unsupported"}),
               "unsupported options should be rejected");
  ok &= Expect(!Parse({L"PrismForge.Launcher.exe", L"--engine"}),
               "an Engine option without a path should be rejected");
  ok &= Expect(!Parse({L"PrismForge.Launcher.exe", L"--control"}),
               "a Control option without a path should be rejected");

  return ok ? 0 : 1;
}
