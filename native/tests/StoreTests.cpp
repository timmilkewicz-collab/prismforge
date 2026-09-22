#include "../engine/ShowStore.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace prismforge;

#define CHECK(condition) do { \
  if (!(condition)) throw std::runtime_error("Check failed: " #condition); \
} while (false)

namespace {
std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Could not read test file");
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}  // namespace

int main() {
  wchar_t temporary[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temporary) > 0);
  const auto root = std::filesystem::path(temporary) /
      (L"PrismForge-StoreTests-" + std::to_wstring(GetCurrentProcessId()) +
       L"-" + std::to_wstring(GetTickCount64()));
  CHECK(std::filesystem::create_directories(root));
  const auto autosave = root / L"autosave.json";
  const std::string rejected = R"({"schema":"ShowBundleV2","version":2})";
  {
    std::ofstream stream(autosave, std::ios::binary);
    CHECK(static_cast<bool>(stream));
    stream << rejected;
  }

  std::filesystem::path preserved;
  {
    ShowStore store(autosave, root / L"Shows");
    ShowState show;
    std::string error;
    CHECK(!store.LoadAutosave(show, error));
    CHECK(!error.empty());
    error.clear();
    CHECK(store.PreserveRejectedAutosave(preserved, error));
    CHECK(error.empty());
    CHECK(!std::filesystem::exists(autosave));
    CHECK(std::filesystem::is_regular_file(preserved));
    CHECK(ReadFile(preserved) == rejected);
    CHECK(store.RequestAutosave(show));
    store.Stop();
    CHECK(std::filesystem::is_regular_file(autosave));
    CHECK(ReadFile(autosave).find("\"schema\": \"ShowBundleV1\"") !=
          std::string::npos);
    CHECK(ReadFile(preserved) == rejected);
  }

  // A directory at the autosave path cannot be quarantined. Callers must
  // leave persistence disabled rather than replacing this original target.
  const auto blocked = root / L"blocked-autosave";
  CHECK(std::filesystem::create_directory(blocked));
  {
    ShowStore store(blocked, root / L"OtherShows");
    std::string error;
    std::filesystem::path unused;
    CHECK(!store.PreserveRejectedAutosave(unused, error));
    CHECK(!error.empty());
    CHECK(std::filesystem::is_directory(blocked));
  }

  CHECK(std::filesystem::remove(autosave));
  CHECK(std::filesystem::remove(preserved));
  CHECK(std::filesystem::remove(blocked));
  CHECK(std::filesystem::remove(root));
  std::cout << "ShowStore rejected-autosave preservation passed.\n";
  return 0;
}
