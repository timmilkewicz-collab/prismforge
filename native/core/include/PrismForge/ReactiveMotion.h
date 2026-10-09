#pragma once

#include "PrismForge/SignalAnalyzer.h"

#include <cstdint>
#include <limits>

namespace prismforge {

// Render-only interpretation of the raw SignalFrameV1 meter. Energy and pulse
// are normalized to [0, 1]; palette follows an eased path within [0, 1].
// Flow is an integrated, non-wrapping phase in [0, 65536] so shader motion
// does not jump when its speed changes or when a cyclic palette reaches an end.
struct ReactiveFrame {
  float energy = 0.0f;
  float pulse = 0.0f;
  float flow = 0.0f;
  float palette = 0.0f;
};

class ReactiveMotion {
 public:
  // Call on every engine iteration, including iterations skipped by a 30 fps
  // output gate. Invalid/rewound clocks hold the prior frame without a jump.
  [[nodiscard]] ReactiveFrame Advance(const SignalFrameV1& signal,
                                      double seconds) noexcept;

 private:
  [[nodiscard]] ReactiveFrame Current() const noexcept;
  void StepPalette() noexcept;

  double lastSeconds_ = std::numeric_limits<double>::quiet_NaN();
  double elapsed_ = 0.0;
  double flow_ = 0.0;
  double sustainedMusicSeconds_ = 0.0;
  double lastPaletteStepSeconds_ = 0.0;
  double paletteTransitionSeconds_ = 0.8;
  float energy_ = 0.0f;
  float pulseAgeSeconds_ = 0.3f;
  float palette_ = 0.0f;
  float paletteFrom_ = 0.0f;
  float paletteTarget_ = 0.0f;
  int paletteDirection_ = 1;
  std::uint64_t lastHitCount_ = 0;
  bool hitCountInitialized_ = false;
  bool lastHitFlag_ = false;
  bool hasPaletteStep_ = false;
};

}  // namespace prismforge
