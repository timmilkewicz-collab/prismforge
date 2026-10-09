#include "PrismForge/ReactiveMotion.h"

#include <algorithm>
#include <cmath>

namespace prismforge {
namespace {
constexpr double kMaxStepSeconds = 0.1;
constexpr double kAttackSeconds = 0.05;
constexpr double kReleaseSeconds = 0.30;
constexpr float kPulseSeconds = 0.30f;
constexpr double kPaletteHoldSeconds = 3.0;
constexpr double kPaletteFallbackSeconds = 8.0;
constexpr double kPaletteBlendSeconds = 0.8;
constexpr double kMaxFlow = 65536.0;

float TargetEnergy(float rms) noexcept {
  if (!std::isfinite(rms) || rms <= 0.003f) return 0.0f;
  // A soft knee makes a distant room mic useful at 0.02 RMS and still leaves
  // headroom at 0.1 RMS; the raw SignalFrame remains untouched for metering.
  const float bounded = std::clamp(rms, 0.0f, 1.0f);
  return std::clamp(1.0f - std::exp(-(bounded - 0.003f) / 0.035f), 0.0f, 1.0f);
}
}  // namespace

ReactiveFrame ReactiveMotion::Current() const noexcept {
  const float tail = std::clamp(1.0f - pulseAgeSeconds_ / kPulseSeconds,
                                0.0f, 1.0f);
  return {
      std::clamp(energy_, 0.0f, 1.0f),
      tail * tail,
      static_cast<float>(std::clamp(flow_, 0.0, kMaxFlow)),
      std::clamp(palette_, 0.0f, 1.0f),
  };
}

void ReactiveMotion::StepPalette() noexcept {
  // Ping-pong through five palette positions rather than wrapping 1 -> 0.
  // The shader can blend spatial pigments while this scalar stays continuous.
  if (paletteTarget_ >= 1.0f) paletteDirection_ = -1;
  if (paletteTarget_ <= 0.0f) paletteDirection_ = 1;
  paletteFrom_ = palette_;
  paletteTarget_ = std::clamp(
      paletteTarget_ + 0.25f * static_cast<float>(paletteDirection_), 0.0f, 1.0f);
  paletteTransitionSeconds_ = 0.0;
  lastPaletteStepSeconds_ = elapsed_;
  sustainedMusicSeconds_ = 0.0;
  hasPaletteStep_ = true;
}

ReactiveFrame ReactiveMotion::Advance(const SignalFrameV1& signal,
                                      double seconds) noexcept {
  if (!std::isfinite(seconds) || seconds < 0.0) return Current();

  if (!std::isfinite(lastSeconds_)) {
    lastSeconds_ = seconds;
  } else if (seconds < lastSeconds_) {
    // A clock rewind should not reverse flow or replay a stale onset. Rebase
    // the input clock while preserving the current visual pose.
    lastSeconds_ = seconds;
    lastHitCount_ = signal.hitCount;
    hitCountInitialized_ = true;
    lastHitFlag_ = signal.hit;
    return Current();
  }

  const double dt = std::clamp(seconds - lastSeconds_, 0.0, kMaxStepSeconds);
  lastSeconds_ = seconds;
  elapsed_ += dt;

  const float target = TargetEnergy(signal.rms);
  const double timeConstant = target > energy_ ? kAttackSeconds : kReleaseSeconds;
  const float response = static_cast<float>(1.0 - std::exp(-dt / timeConstant));
  energy_ = std::clamp(energy_ + (target - energy_) * response, 0.0f, 1.0f);

  bool onset = signal.hit && !lastHitFlag_;
  if (hitCountInitialized_) {
    if (signal.hitCount > lastHitCount_) onset = true;
    if (signal.hitCount < lastHitCount_) onset = false;  // Source/counter reset.
  }
  lastHitCount_ = signal.hitCount;
  hitCountInitialized_ = true;
  lastHitFlag_ = signal.hit;
  pulseAgeSeconds_ = onset ? 0.0f : std::min(
      kPulseSeconds, pulseAgeSeconds_ + static_cast<float>(dt));

  const float pulse = Current().pulse;
  // Flow integrates velocity, rather than multiplying absolute time by audio.
  // Clamping a stalled clock step keeps a single delayed iteration from making
  // a visibly discontinuous leap.
  const double speed = 0.18 + 1.2 * energy_ + 0.40 * pulse;
  flow_ = std::min(kMaxFlow, flow_ + dt * speed);

  const bool activeMusic = target >= 0.18f;
  sustainedMusicSeconds_ = activeMusic ? sustainedMusicSeconds_ + dt : 0.0;
  const bool paletteReady = !hasPaletteStep_ ||
      elapsed_ - lastPaletteStepSeconds_ >= kPaletteHoldSeconds;
  if (activeMusic && paletteReady &&
      (onset || sustainedMusicSeconds_ >= kPaletteFallbackSeconds)) {
    StepPalette();
  }

  paletteTransitionSeconds_ = std::min(
      kPaletteBlendSeconds, paletteTransitionSeconds_ + dt);
  const float progress = static_cast<float>(
      std::clamp(paletteTransitionSeconds_ / kPaletteBlendSeconds, 0.0, 1.0));
  const float ease = progress * progress * (3.0f - 2.0f * progress);
  palette_ = std::clamp(paletteFrom_ +
      (paletteTarget_ - paletteFrom_) * ease, 0.0f, 1.0f);
  return Current();
}

}  // namespace prismforge
