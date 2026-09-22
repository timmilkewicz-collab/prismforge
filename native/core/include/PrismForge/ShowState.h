#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prismforge {

inline constexpr std::array<std::string_view, 12> kSceneIds = {
    "ink-tide", "prism-atrium", "chrome-flock", "signal-lab",
    "neon-rift", "media-alchemy", "reaction-bloom", "harmonic-sand",
    "constellation", "fold-temple", "dream-grove", "living-point-cloud"};

struct DeckState {
  std::string sceneId = "ink-tide";
  std::array<float, 4> effects{};
};

struct OverlayState {
  bool enabled = false;
  std::string sourceId;
  float opacity = 1.0f;
};

struct ShowSnapshot {
  std::array<DeckState, 2> decks{};
  std::array<OverlayState, 4> overlays{};
  std::array<float, 4> masterEffects{};
  float crossfader = 0.0f;
  bool blackout = false;
  bool panicDim = false;
};

enum class Quantization { Immediate, Beat, Bar };

struct ModulationRouteV1 {
  std::string source;
  std::string target;
  float amount = 0.0f;
  float polarity = 1.0f;
  float curve = 1.0f;
  float attackMs = 0.0f;
  float releaseMs = 0.0f;
  float deadband = 0.0f;
  float minValue = 0.0f;
  float maxValue = 1.0f;
  bool enabled = true;
};

class ShowState {
 public:
  static constexpr std::size_t kCueCount = 32;
  static constexpr std::size_t kMaxModulationRoutes = 64;
  using CueBank = std::array<std::optional<ShowSnapshot>, kCueCount>;

  ShowState();
  [[nodiscard]] const ShowSnapshot& Current() const noexcept { return current_; }
  [[nodiscard]] const std::vector<ModulationRouteV1>& Routes() const noexcept { return routes_; }
  [[nodiscard]] const CueBank& Cues() const noexcept { return cues_; }
  bool SetScene(unsigned deck, std::string_view sceneId);
  void SetCrossfader(float value);
  void SetEffect(unsigned deck, unsigned index, float value);
  bool SetOverlay(unsigned slot, OverlayState overlay);
  bool SetMasterEffect(unsigned index, float value);
  void SetBlackout(bool enabled) noexcept { current_.blackout = enabled; }
  void SetPanicDim(bool enabled) noexcept { current_.panicDim = enabled; }
  bool SaveCue(unsigned index);
  bool RecallCue(unsigned index, Quantization quantization, std::uint64_t beatCount);
  // Returns true when a queued beat/bar cue was applied to the current show.
  bool OnBeat(std::uint64_t beatCount);
  bool SetModulation(unsigned slot, ModulationRouteV1 route);
  bool Restore(ShowSnapshot current, CueBank cues,
               std::vector<ModulationRouteV1> routes);
  static float ApplyModulation(float base, float signal, const ModulationRouteV1& route);

 private:
  struct PendingCue { unsigned index; std::uint64_t dueBeat; };
  ShowSnapshot current_{};
  CueBank cues_{};
  std::optional<PendingCue> pendingCue_;
  std::vector<ModulationRouteV1> routes_;
};

}  // namespace prismforge
