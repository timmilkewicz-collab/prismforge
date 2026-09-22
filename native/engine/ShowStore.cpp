#include "ShowStore.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>

namespace prismforge {
namespace {
using Json = nlohmann::json;

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR value = nullptr;
  if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &value))) return {};
  const std::filesystem::path result(value);
  CoTaskMemFree(value);
  return result;
}

Json SnapshotJson(const ShowSnapshot& snapshot) {
  Json decks = Json::array();
  for (const auto& deck : snapshot.decks) {
    decks.push_back({{"sceneId", deck.sceneId}, {"effects", deck.effects}});
  }
  Json overlays = Json::array();
  for (const auto& overlay : snapshot.overlays) {
    overlays.push_back({{"enabled", overlay.enabled},
                        {"sourceId", overlay.sourceId},
                        {"opacity", overlay.opacity}});
  }
  return {{"decks", decks}, {"overlays", overlays},
          {"masterEffects", snapshot.masterEffects},
          {"crossfader", snapshot.crossfader},
          {"blackout", snapshot.blackout}, {"panicDim", snapshot.panicDim}};
}

ShowSnapshot ParseSnapshot(const Json& value) {
  ShowSnapshot result;
  if (value.at("decks").size() != result.decks.size() ||
      value.at("overlays").size() != result.overlays.size() ||
      value.at("masterEffects").size() != result.masterEffects.size()) {
    throw std::invalid_argument("ShowSnapshot array size mismatch");
  }
  for (unsigned i = 0; i < result.decks.size(); ++i) {
    const auto& deck = value.at("decks").at(i);
    if (deck.at("effects").size() != result.decks[i].effects.size()) {
      throw std::invalid_argument("Deck effect count mismatch");
    }
    result.decks[i].sceneId = deck.at("sceneId").get<std::string>();
    result.decks[i].effects = deck.at("effects").get<std::array<float, 4>>();
  }
  for (unsigned i = 0; i < result.overlays.size(); ++i) {
    const auto& overlay = value.at("overlays").at(i);
    result.overlays[i] = {overlay.at("enabled").get<bool>(),
                          overlay.at("sourceId").get<std::string>(),
                          overlay.at("opacity").get<float>()};
  }
  result.masterEffects = value.at("masterEffects").get<std::array<float, 4>>();
  result.crossfader = value.at("crossfader").get<float>();
  result.blackout = value.at("blackout").get<bool>();
  result.panicDim = value.at("panicDim").get<bool>();
  return result;
}

Json BundleJson(const ShowState& show) {
  Json cues = Json::array();
  for (const auto& cue : show.Cues()) {
    cues.push_back(cue ? SnapshotJson(*cue) : Json(nullptr));
  }
  Json routes = Json::array();
  for (const auto& route : show.Routes()) {
    if (route.source.empty() && route.target.empty() && !route.enabled) {
      routes.push_back(nullptr);
      continue;
    }
    routes.push_back({{"source", route.source}, {"target", route.target},
                      {"amount", route.amount}, {"polarity", route.polarity},
                      {"curve", route.curve}, {"attackMs", route.attackMs},
                      {"releaseMs", route.releaseMs},
                      {"deadband", route.deadband},
                      {"minValue", route.minValue},
                      {"maxValue", route.maxValue},
                      {"enabled", route.enabled}});
  }
  return {{"schema", "ShowBundleV1"}, {"version", 1},
          {"show", SnapshotJson(show.Current())},
          {"cues", cues}, {"routes", routes},
          {"palettes", Json::array()}, {"midiMappings", Json::array()},
          {"mediaReferences", Json::array()}};
}

bool ParseBundle(const Json& bundle, ShowState& show) {
  if (bundle.at("schema") != "ShowBundleV1" || bundle.at("version") != 1) {
    return false;
  }
  ShowState::CueBank cues{};
  const auto& cueValues = bundle.at("cues");
  if (!cueValues.is_array() || cueValues.size() != cues.size()) return false;
  for (unsigned i = 0; i < cues.size(); ++i) {
    if (!cueValues.at(i).is_null()) cues[i] = ParseSnapshot(cueValues.at(i));
  }
  std::vector<ModulationRouteV1> routes;
  const auto& routeValues = bundle.at("routes");
  if (!routeValues.is_array() || routeValues.size() > ShowState::kMaxModulationRoutes) {
    return false;
  }
  for (const auto& value : routeValues) {
    ModulationRouteV1 route;
    if (value.is_null()) {
      route.enabled = false;
    } else {
      route.source = value.at("source").get<std::string>();
      route.target = value.at("target").get<std::string>();
      route.amount = value.at("amount").get<float>();
      route.polarity = value.at("polarity").get<float>();
      route.curve = value.at("curve").get<float>();
      route.attackMs = value.at("attackMs").get<float>();
      route.releaseMs = value.at("releaseMs").get<float>();
      route.deadband = value.at("deadband").get<float>();
      route.minValue = value.at("minValue").get<float>();
      route.maxValue = value.at("maxValue").get<float>();
      route.enabled = value.at("enabled").get<bool>();
    }
    routes.push_back(std::move(route));
  }
  return show.Restore(ParseSnapshot(bundle.at("show")), std::move(cues),
                      std::move(routes));
}

bool AtomicWrite(const std::filesystem::path& target, const Json& bundle,
                 std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  if (ec) { error = "Cannot create show directory: " + ec.message(); return false; }
  auto temporary = target;
  temporary += L".tmp." + std::to_wstring(GetCurrentProcessId());
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) { error = "Cannot open temporary show file"; return false; }
    stream << bundle.dump(2);
    stream.flush();
    if (!stream) { error = "Cannot write temporary show file"; return false; }
  }
  if (!MoveFileExW(temporary.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = "Cannot atomically replace show file (Win32 " +
        std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

bool ValidShowName(const std::string& name) {
  return !name.empty() && name.size() <= 64 &&
      std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') ||
               character == '-' || character == '_' || character == ' ';
      });
}

bool ReadBundleFile(const std::filesystem::path& path, ShowState& show,
                    std::string& error) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    error = "Show file does not exist";
    return false;
  }
  const auto bytes = std::filesystem::file_size(path, ec);
  if (ec || bytes > 1024 * 1024) {
    error = "Show file exceeds 1 MiB or cannot be read";
    return false;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) { error = "Cannot open show file"; return false; }
  try {
    if (ParseBundle(Json::parse(stream), show)) return true;
    error = "Show schema or state validation failed";
    return false;
  } catch (const std::exception& exception) {
    error = std::string("Cannot parse show: ") + exception.what();
    return false;
  }
}
}  // namespace

ShowStore::ShowStore() {
  const auto roaming = KnownFolder(FOLDERID_RoamingAppData);
  const auto documents = KnownFolder(FOLDERID_Documents);
  if (roaming.empty() || documents.empty()) return;
  autosavePath_ = roaming / "PrismForge" / "autosave.json";
  showsDirectory_ = documents / "PrismForge" / "Shows";
  worker_ = std::thread([this] { Worker(); });
}

ShowStore::ShowStore(std::filesystem::path autosavePath,
                     std::filesystem::path showsDirectory)
    : autosavePath_(std::move(autosavePath)),
      showsDirectory_(std::move(showsDirectory)) {
  if (Ready()) worker_ = std::thread([this] { Worker(); });
}

ShowStore::~ShowStore() { Stop(); }

bool ShowStore::LoadAutosave(ShowState& show, std::string& error) const {
  if (!Ready()) { error = "Known-folder lookup failed"; return false; }
  if (!std::filesystem::exists(autosavePath_)) return false;
  return ReadBundleFile(autosavePath_, show, error);
}

bool ShowStore::PreserveRejectedAutosave(std::filesystem::path& preservedPath,
                                         std::string& error) const {
  if (!Ready()) { error = "Known-folder lookup failed"; return false; }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(autosavePath_, ec)) {
    error = "Rejected autosave is not a regular file";
    return false;
  }
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  const auto stamp = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) |
      now.dwLowDateTime;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    auto candidate = autosavePath_;
    candidate += L".rejected." + std::to_wstring(stamp) + L"." +
        std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(attempt);
    // No MOVEFILE_REPLACE_EXISTING: recovery must never overwrite another file.
    if (MoveFileExW(autosavePath_.c_str(), candidate.c_str(),
                    MOVEFILE_WRITE_THROUGH)) {
      preservedPath = std::move(candidate);
      return true;
    }
    const DWORD code = GetLastError();
    if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS) {
      error = "Cannot preserve rejected autosave (Win32 " +
          std::to_string(code) + ")";
      return false;
    }
  }
  error = "Could not find an unused rejected-autosave name";
  return false;
}

bool ShowStore::LoadPortable(ShowState& show, const std::string& name,
                             std::string& error) const {
  if (!Ready()) { error = "Known-folder lookup failed"; return false; }
  if (!ValidShowName(name)) { error = "Invalid portable show name"; return false; }
  return ReadBundleFile(showsDirectory_ / (name + ".prismforge.json"), show,
                        error);
}

bool ShowStore::Queue(SaveJob job) {
  std::lock_guard lock(mutex_);
  if (stopping_ || pending_.size() >= 4) return false;
  pending_.push_back(std::move(job));
  signal_.notify_one();
  return true;
}

bool ShowStore::RequestAutosave(const ShowState& show) {
  return Ready() && Queue({autosavePath_, BundleJson(show)});
}

bool ShowStore::RequestPortableSave(const ShowState& show,
                                     const std::string& name,
                                     std::string& error) {
  if (!Ready()) { error = "Known-folder lookup failed"; return false; }
  if (!ValidShowName(name)) {
    error = "Show name must use 1-64 letters, numbers, spaces, - or _";
    return false;
  }
  if (!Queue({showsDirectory_ / (name + ".prismforge.json"), BundleJson(show)})) {
    error = "Show-save queue full";
    return false;
  }
  return true;
}

void ShowStore::Worker() {
  for (;;) {
    SaveJob job;
    {
      std::unique_lock lock(mutex_);
      signal_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (pending_.empty() && stopping_) return;
      job = std::move(pending_.front());
      pending_.pop_front();
    }
    std::string error;
    if (!AtomicWrite(job.path, job.bundle, error)) {
      std::cerr << "Show save failed: " << error << '\n';
    }
  }
}

void ShowStore::Stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  signal_.notify_all();
  if (worker_.joinable()) worker_.join();
}

}  // namespace prismforge
