#pragma once

#include "PrismForge/SignalAnalyzer.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace prismforge {

// Configuration is sanitized by MusicalStateEngine. The seed is deliberately
// used only when an event ID is minted; changing it cannot change state weights.
struct MusicalStateEngineConfig {
  unsigned sampleRate = 48000;
  std::uint64_t seed = 0;
  float slowStateAgeHorizonSeconds = 30.0f;
  float maxDeltaSeconds = 0.25f;
};

// Renderer-neutral, replayable description of musical behavior. All values
// except energyTrend are in [0, 1]; energyTrend is in [-1, 1]. slowStateAge is
// the normalized age of the currently committed slow state, capped at the
// configured age horizon. States are independent weights and may overlap.
//
// Keep this ABI-friendly contract free of renderer or scene concepts so a
// future shared PrismForge/PrismBurst component can consume it unchanged.
struct alignas(16) MusicalStateFrameV1 {
  std::uint64_t sourceSampleIndex = 0;
  std::uint64_t eventId = 0;
  float immediateEnergy = 0.0f;
  float onsetEnvelope = 0.0f;
  float accentEnvelope = 0.0f;
  float grooveEnergy = 0.0f;
  float sustainedEnergy = 0.0f;
  float energyTrend = 0.0f;
  float calm = 0.0f;
  float building = 0.0f;
  float driving = 0.0f;
  float peak = 0.0f;
  float release = 0.0f;
  float slowStateAge = 0.0f;
};

static_assert(sizeof(MusicalStateFrameV1) == 64);
static_assert(alignof(MusicalStateFrameV1) == 16);
static_assert(std::is_standard_layout_v<MusicalStateFrameV1>);
static_assert(std::is_trivially_copyable_v<MusicalStateFrameV1>);

class MusicalStateEngine {
 public:
  explicit MusicalStateEngine(
      MusicalStateEngineConfig config = {}) noexcept;

  // Reset is deterministic and retains configuration. The next Advance call
  // establishes a fresh source anchor without replaying stale hit flags.
  // The reset frame is all-zero/unknown; calm is inferred from subsequent
  // silent samples rather than asserted before any observation exists.
  void Reset() noexcept;

  // A rate change invalidates the sample-index clock and therefore resets all
  // temporal state. A zero rate is sanitized to 48000 Hz.
  void SetSampleRate(unsigned sampleRate) noexcept;
  [[nodiscard]] unsigned SampleRate() const noexcept {
    return config_.sampleRate;
  }

  [[nodiscard]] const MusicalStateFrameV1& Current() const noexcept {
    return frame_;
  }

  // Time advances only by (new sampleIndex - old sampleIndex) / sampleRate.
  // Duplicate indices return the exact current frame. A rewound sample clock
  // or source-counter rewind performs an explicit deterministic reset/rebase.
  [[nodiscard]] MusicalStateFrameV1 Advance(
      const SignalFrameV1& signal) noexcept;

 private:
  enum class SlowState : std::uint8_t {
    none = 0,
    calm,
    building,
    driving,
    peak,
    release,
  };

  void Anchor(const SignalFrameV1& signal) noexcept;
  void UpdateSlowState(float dt) noexcept;
  [[nodiscard]] std::uint64_t MakeEventId(SlowState state) noexcept;

  MusicalStateFrameV1 frame_{};
  MusicalStateEngineConfig config_{};
  std::uint64_t lastSampleIndex_ = 0;
  std::uint64_t lastHitCount_ = 0;
  std::uint64_t lastAccentCount_ = 0;
  std::uint64_t lastHitSampleIndex_ = 0;
  std::uint64_t previousHitIntervalSamples_ = 0;
  std::uint64_t eventOrdinal_ = 0;
  float longTermEnergy_ = 0.0f;
  float trend_ = 0.0f;
  float buildupMemory_ = 0.0f;
  float peakMemory_ = 0.0f;
  float slowStateAgeSeconds_ = 0.0f;
  float candidateAgeSeconds_ = 0.0f;
  SlowState slowState_ = SlowState::none;
  SlowState candidateState_ = SlowState::none;
  bool initialized_ = false;
  bool lastHitFlag_ = false;
  bool lastAccentFlag_ = false;
  bool haveHitSample_ = false;
  // Account explicitly for the frame's 16-byte alignment so MSVC /W4 does
  // not report benign implicit class padding (C4324).
  std::array<std::uint8_t, 10> reservedAlignment_{};
};

}  // namespace prismforge
