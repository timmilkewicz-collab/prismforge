#include "AudioSwitcher.h"
#include "MidiBridge.h"
#include "OscBridge.h"
#include "PipeServer.h"
#include "ShowStore.h"
#include "SpoutRender.h"

#include "PrismForge/QualityGovernor.h"
#include "PrismForge/ShowState.h"
#include "PrismForge/SignalAnalyzer.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace prismforge;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {
std::atomic_bool g_running{true};

BOOL WINAPI OnConsoleEvent(DWORD event) {
  if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
    g_running = false;
    return TRUE;
  }
  return FALSE;
}

std::filesystem::path ExecutableDirectory() {
  std::wstring path(32768, L'\0');
  const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  path.resize(size);
  return std::filesystem::path(path).parent_path();
}

Json ReadSceneCatalog(const std::filesystem::path& executableDirectory) {
  std::ifstream stream(executableDirectory / "assets/scenes/scenes-v1.json");
  if (!stream) return Json::array();
  try {
    const Json manifest = Json::parse(stream);
    Json catalog = Json::array();
    for (const auto& scene : manifest.at("scenes")) {
      catalog.push_back({{"id", scene.at("id")}, {"name", scene.at("name")},
                         {"category", "procedural"}});
    }
    return catalog;
  } catch (const std::exception&) {
    return Json::array();
  }
}

bool SceneAvailable(const Json& catalog, const std::string& id) {
  return std::any_of(catalog.begin(), catalog.end(), [&](const Json& item) {
    return item.value("id", std::string{}) == id;
  });
}

Json SignalJson(const SignalFrameV1& signal, double fps, double frameMs,
                unsigned targetFps, unsigned tier, float gestureMotion,
                const std::string& gestureName) {
  return {
      {"sampleIndex", signal.sampleIndex}, {"rms", signal.rms},
      {"peak", signal.peak}, {"bands", signal.bands},
      {"low", signal.bass}, {"mid", signal.mids}, {"high", signal.highs},
      {"hit", signal.hit}, {"accent", signal.accent},
      {"hitCount", signal.hitCount}, {"bpm", signal.bpm},
      {"beatPhase", signal.beatPhase}, {"beatConfidence", signal.beatConfidence},
      {"gesture", {{"motion", gestureMotion}, {"name", gestureName}}},
      {"audio", {{"rms", signal.rms}, {"peak", signal.peak},
                  {"low", signal.bass}, {"mid", signal.mids},
                  {"high", signal.highs}, {"clipping", signal.peak >= 0.99f}}},
      {"performance", {{"fps", fps}, {"targetFps", targetFps},
                       {"frameTimeMs", frameMs}, {"adaptiveQuality", tier}}}};
}

Json DeckJson(const DeckState& deck, const std::vector<ModulationRouteV1>& routes,
              char deckName) {
  static constexpr const char* names[] = {"Bloom", "Feedback", "Kaleidoscope", "Pixelate"};
  Json effects = Json::array();
  for (unsigned index = 0; index < deck.effects.size(); ++index) {
    effects.push_back({{"id", index}, {"name", names[index]},
                       {"amount", deck.effects[index]}});
  }
  Json modulations = Json::array();
  const std::string prefix = std::string(1, deckName) + ":";
  for (unsigned index = 0; index < routes.size(); ++index) {
    const auto& route = routes[index];
    if (route.target.rfind(prefix, 0) != 0) continue;
    modulations.push_back({{"slot", index}, {"source", route.source},
                           {"target", route.target.substr(prefix.size())},
                           {"amount", route.amount},
                           {"smoothing", route.attackMs / 1000.0f},
                           {"enabled", route.enabled}});
  }
  return {{"sceneId", deck.sceneId}, {"effects", effects},
          {"modulations", modulations}};
}

Json SnapshotJson(const ShowState& show, const SignalFrameV1& signal,
                  const Json& sceneCatalog, const std::vector<AudioDeviceInfo>& devices,
                  const std::string& sourceId, bool audioConnected,
                  bool spoutReady, double fps, double frameMs,
                  unsigned targetFps, unsigned tier, std::uint64_t revision,
                  const std::array<bool, ShowState::kCueCount>& cueSaved,
                  bool oscReady, const Json& prismBurstHealth) {
  const auto& state = show.Current();
  Json sources = Json::array();
  sources.push_back({{"id", "system-default"}, {"name", "System audio (default)"},
                     {"kind", "loopback"}, {"connected", true}});
  for (const auto& device : devices) {
    sources.push_back({{"id", device.id}, {"name", device.name},
                       {"kind", device.loopback ? "loopback" : "input"},
                       {"connected", true}});
  }
  Json cues = Json::array();
  for (unsigned i = 0; i < cueSaved.size(); ++i) {
    cues.push_back({{"index", i}, {"saved", cueSaved[i]}});
  }
  return {
      {"revision", revision},
      {"decks", {{"A", DeckJson(state.decks[0], show.Routes(), 'A')},
                 {"B", DeckJson(state.decks[1], show.Routes(), 'B')}}},
      {"crossfader", state.crossfader},
      {"masterEffects", state.masterEffects},
      {"blackout", state.blackout}, {"panicDim", state.panicDim},
      {"sceneCatalog", sceneCatalog},
      {"audio", {{"sourceId", sourceId}, {"sources", sources},
                 {"connected", audioConnected}, {"rms", signal.rms},
                 {"peak", signal.peak}, {"low", signal.bass},
                 {"mid", signal.mids}, {"high", signal.highs},
                 {"clipping", signal.peak >= 0.99f}}},
      {"cues", cues},
      {"performance", {{"fps", fps}, {"targetFps", targetFps},
                       {"frameTimeMs", frameMs}, {"adaptiveQuality", tier}}},
      {"output", {{"spout", {{"ready", spoutReady},
                              {"connected", false},  // Receiver presence is not instrumented.
                              {"senderName", "PrismForge"}}},
                  {"width", 1920}, {"height", 1080}}},
      {"osc", {{"ready", oscReady}, {"commandPort", 12100},
                {"statusPort", 12101}, {"gesturePort", 12002}}},
      {"prismBurst", prismBurstHealth}};
}

bool ApplyCommand(const QueuedCommand& command, ShowState& show,
                  const Json& catalog, AudioSwitcher& audio,
                  ShowStore& store,
                  bool persistenceEnabled,
                  std::array<bool, ShowState::kCueCount>& cueSaved,
                  std::uint64_t beatCount, std::string& error) {
  try {
    const Json& payload = command.payload;
    const auto deckIndex = [&]() -> unsigned {
      const std::string deck = payload.at("deck").get<std::string>();
      if (deck == "A") return 0;
      if (deck == "B") return 1;
      throw std::invalid_argument("Deck must be A or B");
    };
    if (command.name == "requestSnapshot") return false;
    if (command.name == "setScene") {
      const auto sceneId = payload.at("sceneId").get<std::string>();
      if (!SceneAvailable(catalog, sceneId) || !show.SetScene(deckIndex(), sceneId)) {
        throw std::invalid_argument("Scene unavailable");
      }
    } else if (command.name == "setCrossfader") {
      show.SetCrossfader(payload.at("value").get<float>());
    } else if (command.name == "setBlackout") {
      show.SetBlackout(payload.at("enabled").get<bool>());
    } else if (command.name == "setPanicDim") {
      show.SetPanicDim(payload.at("enabled").get<bool>());
    } else if (command.name == "setEffect") {
      const unsigned effectIndex = payload.at("effectIndex").get<unsigned>();
      if (effectIndex >= 4) throw std::invalid_argument("Effect index invalid");
      show.SetEffect(deckIndex(), effectIndex, payload.at("amount").get<float>());
    } else if (command.name == "setOverlay") {
      OverlayState overlay;
      overlay.enabled = payload.at("enabled").get<bool>();
      overlay.sourceId = payload.at("sourceId").get<std::string>();
      overlay.opacity = payload.at("opacity").get<float>();
      if (!show.SetOverlay(payload.at("slot").get<unsigned>(), std::move(overlay))) {
        throw std::invalid_argument("Overlay is invalid");
      }
    } else if (command.name == "setMasterEffect") {
      if (!show.SetMasterEffect(payload.at("index").get<unsigned>(),
                                payload.at("amount").get<float>())) {
        throw std::invalid_argument("Master effect is invalid");
      }
    } else if (command.name == "setModulation") {
      const unsigned slot = payload.at("slot").get<unsigned>();
      ModulationRouteV1 route;
      route.source = payload.at("source").get<std::string>();
      route.target = payload.at("deck").get<std::string>() + ":" +
          payload.at("target").get<std::string>();
      route.amount = payload.at("amount").get<float>();
      route.attackMs = payload.value("smoothing", 0.0f) * 1000.0f;
      route.releaseMs = route.attackMs;
      route.enabled = payload.value("enabled", true);
      if (!show.SetModulation(slot, std::move(route))) {
        throw std::invalid_argument("Invalid modulation route");
      }
    } else if (command.name == "saveCue") {
      const unsigned index = payload.at("index").get<unsigned>();
      if (!show.SaveCue(index)) throw std::invalid_argument("Cue index invalid");
      cueSaved[index] = true;
    } else if (command.name == "recallCue") {
      const unsigned index = payload.at("index").get<unsigned>();
      const auto quantization = payload.value("quantization", std::string("immediate"));
      const auto mode = quantization == "bar" ? Quantization::Bar :
          quantization == "beat" ? Quantization::Beat : Quantization::Immediate;
      if (!show.RecallCue(index, mode, beatCount)) {
        throw std::invalid_argument("Cue is empty or index invalid");
      }
    } else if (command.name == "setAudioSource") {
      const std::string id = payload.at("id").get<std::string>();
      if (!audio.Request(id, error)) return false;
      // The worker reports success/failure later; no show revision changes yet.
      return false;
    } else if (command.name == "saveShow") {
      if (!persistenceEnabled) throw std::invalid_argument("Persistence disabled");
      if (!store.RequestPortableSave(show, payload.at("name").get<std::string>(), error)) {
        return false;
      }
    } else if (command.name == "loadShow") {
      if (!persistenceEnabled) throw std::invalid_argument("Persistence disabled");
      if (!store.LoadPortable(show, payload.at("name").get<std::string>(), error)) {
        return false;
      }
      for (unsigned i = 0; i < cueSaved.size(); ++i) {
        cueSaved[i] = show.Cues()[i].has_value();
      }
    } else {
      throw std::invalid_argument("Unknown action: " + command.name);
    }
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
}

float SourceValue(const std::string& source, const SignalFrameV1& signal,
                  float gestureMotion) {
  if (source == "audio.kick") return signal.hit ? 1.0f : 0.0f;
  if (source == "audio.accent") return signal.accent ? 1.0f : 0.0f;
  if (source == "audio.low") return signal.bass;
  if (source == "audio.mid") return signal.mids;
  if (source == "audio.high") return signal.highs;
  if (source == "audio.rms") return signal.rms;
  if (source == "tempo.phase") return signal.beatPhase;
  if (source == "kinect.motion") return gestureMotion;
  return 0.0f;  // Unknown sources never invent signal.
}

ShowSnapshot EffectiveShow(const ShowState& show, const SignalFrameV1& signal,
                           std::array<float, ShowState::kMaxModulationRoutes>& smoothed,
                           float deltaSeconds, float gestureMotion) {
  ShowSnapshot effective = show.Current();
  const auto& routes = show.Routes();
  for (unsigned slot = 0; slot < routes.size(); ++slot) {
    const auto& route = routes[slot];
    if (!route.enabled || route.source.empty()) continue;
    const float raw = std::clamp(SourceValue(route.source, signal, gestureMotion),
                                 0.0f, 1.0f);
    const float milliseconds = raw > smoothed[slot] ? route.attackMs : route.releaseMs;
    const float alpha = milliseconds <= 0.0f ? 1.0f :
        1.0f - std::exp(-deltaSeconds * 1000.0f / milliseconds);
    smoothed[slot] += (raw - smoothed[slot]) * std::clamp(alpha, 0.0f, 1.0f);
    if (route.target.ends_with("master.crossfader")) {
      effective.crossfader = ShowState::ApplyModulation(
          effective.crossfader, smoothed[slot], route);
      continue;
    }
    if (route.target.size() != 15 || route.target[1] != ':' ||
        route.target.substr(2, 12) != "deck.effect." ||
        route.target[14] < '0' || route.target[14] > '3') continue;
    const unsigned deck = route.target[0] == 'A' ? 0 : route.target[0] == 'B' ? 1 : 2;
    if (deck >= 2) continue;
    const unsigned effect = route.target[14] - '0';
    effective.decks[deck].effects[effect] = ShowState::ApplyModulation(
        effective.decks[deck].effects[effect], smoothed[slot], route);
  }
  return effective;
}
}  // namespace

int main(int argc, char** argv) {
  SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\PrismForge.Engine");
  if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
    std::cerr << "PrismForge Engine is already running.\n";
    if (mutex) CloseHandle(mutex);
    return 2;
  }
  int seconds = 0;
  bool noAudio = false;
  bool noPersist = false;
  bool launchpadEnabled = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--seconds" && index + 1 < argc) {
      seconds = std::clamp(std::atoi(argv[++index]), 1, 7200);
    } else if (argument == "--no-audio") {
      noAudio = true;
    } else if (argument == "--no-persist") {
      noPersist = true;
    } else if (argument == "--launchpad") {
      launchpadEnabled = true;
    } else {
      std::cerr << "Usage: PrismForge.Engine [--seconds N] [--no-audio] "
                   "[--no-persist] [--launchpad]\n";
      ReleaseMutex(mutex);
      CloseHandle(mutex);
      return 64;
    }
  }

  const auto executableDirectory = ExecutableDirectory();
  const auto sceneCatalog = ReadSceneCatalog(executableDirectory);
  if (sceneCatalog.empty()) {
    std::cerr << "No valid scene manifest beside executable.\n";
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 3;
  }
  SpoutRender renderer;
  std::string error;
  if (!renderer.Initialize(executableDirectory / "assets/shaders", error)) {
    std::cerr << "Renderer initialization failed: " << error << '\n';
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 4;
  }

  BoundedQueue<AudioBlock> audioQueue(64);
  BoundedQueue<QueuedCommand> commands(128);
  BoundedQueue<GestureEvent> gestures(128);
  BoundedQueue<MidiMessage> midiMessages(128);
  MidiBridge midi(midiMessages);
  LaunchpadSInterpreter launchpad;
  AudioSwitcher audio(audioQueue);
  std::string audioSource = "system-default";
  if (!audio.Start(!noAudio, error)) {
    std::cerr << "Audio worker unavailable; visuals continue: " << error << '\n';
  }
  std::vector<AudioDeviceInfo> devices;
  PipeServer pipe(commands);
  pipe.Start();
  OscBridge osc(commands, gestures);
  error.clear();
  const bool oscReady = osc.Start(error);
  if (!oscReady) std::cerr << "OSC unavailable; pipe control continues: " << error << '\n';

  SignalAnalyzer analyzer(audio.SampleRate());
  ShowState show;
  ShowStore store;
  bool persistenceEnabled = !noPersist;
  error.clear();
  if (persistenceEnabled) {
    if (store.LoadAutosave(show, error)) {
      std::cout << "Restored autosave from " << store.AutosavePath() << '\n';
    } else if (!error.empty()) {
      std::cerr << "Autosave rejected: " << error << '\n';
      std::filesystem::path preserved;
      error.clear();
      if (store.PreserveRejectedAutosave(preserved, error)) {
        std::cerr << "Rejected autosave preserved at " << preserved << '\n';
      } else {
        persistenceEnabled = false;
        std::cerr << "Autosave disabled for this run: " << error << '\n';
      }
    }
  }
  QualityGovernor governor;
  std::array<bool, ShowState::kCueCount> cueSaved{};
  for (unsigned i = 0; i < cueSaved.size(); ++i) cueSaved[i] = show.Cues()[i].has_value();
  if (launchpadEnabled) {
    error.clear();
    if (!ValidateLaunchpadSProfile() || !midi.OpenLaunchpadS(true, error)) {
      std::cerr << "Launchpad S unavailable; visuals continue: "
                << (error.empty() ? "profile validation failed" : error) << '\n';
    } else {
      std::cout << "Launchpad S input and mapped-only LED feedback enabled.\n";
    }
  }
  bool launchpadYieldAnnounced = false;
  std::array<float, ShowState::kMaxModulationRoutes> routeSmoothing{};
  float gestureMotion = 0.0f;
  std::string gestureName;
  Json prismBurstHealth = OscBridge::ReadPrismBurstHealth();
  std::uint64_t revision = 1;
  std::uint64_t beatCount = 0;
  std::uint64_t lastHitCount = 0;
  std::uint64_t lastAccentCount = 0;
  std::uint64_t lastSavedRevision = 0;
  double measuredFps = 0.0;
  double renderMs = 0.0;
  unsigned framesThisSecond = 0;
  const auto start = Clock::now();
  auto nextFrame = start;
  auto lastFps = start;
  auto lastPublish = Clock::time_point{};
  auto lastAutosave = start;
  auto lastHealth = start;
  while (g_running && (seconds == 0 || Clock::now() - start < std::chrono::seconds(seconds))) {
    while (auto event = audio.Poll()) {
      if (event->hasDevices) {
        devices = std::move(event->devices);
        ++revision;
      }
      if (event->kind == AudioSwitchEventKind::Switched) {
        audioSource = event->activeId;
        while (audioQueue.TryPop()) {}  // Drop blocks from the prior sample clock.
        analyzer = SignalAnalyzer(event->sampleRate);
        lastHitCount = 0;
        lastAccentCount = 0;
        ++revision;
      } else if (event->kind == AudioSwitchEventKind::Disconnected) {
        ++revision;
      }
      if (!event->error.empty()) {
        std::cerr << "Audio device: " << event->error << '\n';
        pipe.ReportError(event->error, "");
      }
    }
    gestureMotion *= 0.94f;
    while (auto gesture = gestures.TryPop()) {
      gestureMotion = std::max(gestureMotion, gesture->strength);
      gestureName = gesture->name;
    }
    while (auto block = audioQueue.TryPop()) {
      analyzer.PushMono(block->samples.data(), block->count);
    }
    SignalFrameV1 signal = analyzer.Latest();
    if (signal.hitCount > lastHitCount) {
      signal.hit = true;
      signal.accent = signal.accentCount > lastAccentCount;
      beatCount += signal.hitCount - lastHitCount;
      lastHitCount = signal.hitCount;
      lastAccentCount = signal.accentCount;
      if (show.OnBeat(beatCount)) ++revision;
    } else {
      signal.hit = false;
      signal.accent = false;
    }
    while (auto command = commands.TryPop()) {
      error.clear();
      const bool changed = command->name == "reloadShaders" ?
          renderer.ReloadShaders(error) :
          ApplyCommand(*command, show, sceneCatalog, audio, store,
                       persistenceEnabled, cueSaved, beatCount, error);
      if (changed) ++revision;
      if (!error.empty()) {
        std::cerr << "Command error: " << error << '\n';
        pipe.ReportError(error, command->requestId);
      }
    }
    while (auto message = midiMessages.TryPop()) {
      if (midi.YieldedToPrismBurst()) continue;
      const auto action = launchpad.Process(*message);
      if (!action || midi.YieldedToPrismBurst()) continue;
      bool changed = false;
      switch (action->type) {
        case LaunchpadActionType::SetScene:
          changed = show.SetScene(action->deck, kSceneIds[action->index]);
          break;
        case LaunchpadActionType::RecallCue:
          changed = show.RecallCue(action->index, Quantization::Immediate, beatCount);
          break;
        case LaunchpadActionType::SaveCue:
          changed = show.SaveCue(action->index);
          if (changed) cueSaved[action->index] = true;
          break;
        case LaunchpadActionType::SetCrossfader:
          show.SetCrossfader(action->value);
          changed = true;
          break;
        case LaunchpadActionType::ToggleBlackout:
          show.SetBlackout(!show.Current().blackout);
          changed = true;
          break;
        case LaunchpadActionType::TogglePanicDim:
          show.SetPanicDim(!show.Current().panicDim);
          changed = true;
          break;
      }
      if (changed) ++revision;
    }
    if (midi.YieldedToPrismBurst() && !launchpadYieldAnnounced) {
      launchpad.Reset();
      launchpadYieldAnnounced = true;
      std::cerr << "Launchpad S yielded to PrismBurst; MIDI input and LED output "
                   "are closed. Restart PrismForge to opt in again.\n";
    }
    if (midi.FeedbackEnabled()) {
      std::array<bool, 8> firstEightCues{};
      std::copy_n(cueSaved.begin(), firstEightCues.size(), firstEightCues.begin());
      (void)midi.QueueFeedback(BuildLaunchpadSLeds(
          show.Current(), firstEightCues, launchpad.Pressed()));
    }
    const auto renderStart = Clock::now();
    const double elapsed = std::chrono::duration<double>(renderStart - start).count();
    error.clear();
    const auto effective = EffectiveShow(show, signal, routeSmoothing,
        1.0f / governor.Current().targetFps, gestureMotion);
    if (!renderer.Render(effective, signal, governor.Current(), elapsed, error)) {
      std::cerr << "Renderer error: " << error << '\n';
      break;
    }
    renderMs = std::chrono::duration<double, std::milli>(Clock::now() - renderStart).count();
    governor.ObserveFrameMilliseconds(renderMs);
    ++framesThisSecond;
    const auto now = Clock::now();
    if (now - lastFps >= std::chrono::seconds(1)) {
      measuredFps = framesThisSecond / std::chrono::duration<double>(now - lastFps).count();
      framesThisSecond = 0;
      lastFps = now;
      std::cout << "fps=" << measuredFps << " tier=" << governor.TierIndex()
                << " audio=" << signal.rms << " clients=" << pipe.ClientConnected() << '\n';
      if (oscReady) (void)osc.SendStatus(static_cast<float>(measuredFps),
          static_cast<std::int32_t>(governor.TierIndex()), signal.rms, true);
    }
    if (now - lastHealth >= std::chrono::seconds(5)) {
      prismBurstHealth = OscBridge::ReadPrismBurstHealth();
      lastHealth = now;
    }
    if (now - lastPublish >= std::chrono::milliseconds(50)) {
      pipe.Publish(SnapshotJson(show, signal, sceneCatalog, devices, audioSource,
                                audio.IsRunning(), true, measuredFps, renderMs,
                                governor.Current().targetFps, governor.TierIndex(),
                                revision, cueSaved, oscReady, prismBurstHealth),
                   SignalJson(signal, measuredFps, renderMs,
                              governor.Current().targetFps, governor.TierIndex(),
                              gestureMotion, gestureName));
      lastPublish = now;
    }
    if (persistenceEnabled && now - lastAutosave >= std::chrono::seconds(10) &&
        revision != lastSavedRevision) {
      if (store.RequestAutosave(show)) lastSavedRevision = revision;
      lastAutosave = now;
    }
    nextFrame += std::chrono::nanoseconds(
        1'000'000'000 / governor.Current().targetFps);
    std::this_thread::sleep_until(nextFrame);
    if (nextFrame < Clock::now() - std::chrono::milliseconds(100)) nextFrame = Clock::now();
  }
  pipe.Stop();
  osc.Stop();
  midi.Stop();
  if (persistenceEnabled) (void)store.RequestAutosave(show);
  store.Stop();
  audio.Stop();
  renderer.Shutdown();
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
