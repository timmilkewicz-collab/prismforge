#include "PrismForge/MusicalStateEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace prismforge {
namespace {

constexpr unsigned kDefaultSampleRate = 48000;
constexpr float kMinimumAgeHorizonSeconds = 1.0f;
constexpr float kMaximumAgeHorizonSeconds = 600.0f;
constexpr float kMinimumDeltaSeconds = 1.0e-4f;
constexpr float kMaximumDeltaSeconds = 1.0f;

struct SanitizedSignal {
  std::uint64_t sampleIndex = 0;
  float rms = 0.0f;
  float peak = 0.0f;
  std::array<float, 32> bands{};
  float bass = 0.0f;
  float mids = 0.0f;
  float highs = 0.0f;
  bool hit = false;
  bool accent = false;
  std::uint64_t hitCount = 0;
  std::uint64_t accentCount = 0;
  float bpm = 0.0f;
  float beatPhase = 0.0f;
  float beatConfidence = 0.0f;
};

float Unit(float value) noexcept {
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

SanitizedSignal Sanitize(const SignalFrameV1& source) noexcept {
  SanitizedSignal signal{};
  signal.sampleIndex = source.sampleIndex;
  signal.rms = Unit(source.rms);
  signal.peak = Unit(source.peak);
  for (std::size_t i = 0; i < signal.bands.size(); ++i) {
    signal.bands[i] = Unit(source.bands[i]);
  }
  signal.bass = Unit(source.bass);
  signal.mids = Unit(source.mids);
  signal.highs = Unit(source.highs);
  signal.hit = source.hit;
  signal.accent = source.accent;
  signal.hitCount = source.hitCount;
  signal.accentCount = source.accentCount;
  signal.bpm = std::isfinite(source.bpm)
      ? std::clamp(source.bpm, 0.0f, 400.0f)
      : 0.0f;
  signal.beatPhase = Unit(source.beatPhase);
  signal.beatConfidence = Unit(source.beatConfidence);
  return signal;
}

float SoftKnee(float value, float floor, float width) noexcept {
  if (value <= floor) return 0.0f;
  return Unit(1.0f - std::exp(-(value - floor) / width));
}

float SmoothStep(float low, float high, float value) noexcept {
  const float t = Unit((value - low) / (high - low));
  return t * t * (3.0f - 2.0f * t);
}

float Follow(float current, float target, float dt, float attackSeconds,
             float releaseSeconds) noexcept {
  target = Unit(target);
  const float timeConstant = target > current ? attackSeconds : releaseSeconds;
  const float response = Unit(1.0f - std::exp(-dt / timeConstant));
  return Unit(current + (target - current) * response);
}

float ImmediateEnergy(const SanitizedSignal& signal) noexcept {
  float bandTotal = 0.0f;
  for (const float band : signal.bands) bandTotal += band;
  const float bandAverage = bandTotal / static_cast<float>(signal.bands.size());
  const float summarizedSpectrum =
      (signal.bass + signal.mids + signal.highs) / 3.0f;
  const float spectrum = std::max(bandAverage, summarizedSpectrum);

  // Preserve useful separation across room-mic RMS values while bounding
  // clipped peaks and malformed spectral fields before they enter memory.
  const float level = SoftKnee(signal.rms, 0.003f, 0.055f);
  const float peak = SoftKnee(signal.peak, 0.010f, 0.18f);
  return Unit(level * 0.88f + peak * 0.08f + spectrum * 0.04f);
}

std::uint64_t Mix64(std::uint64_t value) noexcept {
  // SplitMix64 finalizer: fixed integer operations give stable IDs across
  // compilers without creating an RNG whose state could affect confidences.
  value ^= value >> 30;
  value *= UINT64_C(0xbf58476d1ce4e5b9);
  value ^= value >> 27;
  value *= UINT64_C(0x94d049bb133111eb);
  value ^= value >> 31;
  return value;
}

}  // namespace

MusicalStateEngine::MusicalStateEngine(
    MusicalStateEngineConfig config) noexcept
    : config_(config) {
  config_.sampleRate = config_.sampleRate == 0
      ? kDefaultSampleRate
      : config_.sampleRate;
  config_.slowStateAgeHorizonSeconds =
      std::isfinite(config_.slowStateAgeHorizonSeconds)
      ? std::clamp(config_.slowStateAgeHorizonSeconds,
                   kMinimumAgeHorizonSeconds, kMaximumAgeHorizonSeconds)
      : 30.0f;
  config_.maxDeltaSeconds = std::isfinite(config_.maxDeltaSeconds)
      ? std::clamp(config_.maxDeltaSeconds, kMinimumDeltaSeconds,
                   kMaximumDeltaSeconds)
      : 0.25f;
  Reset();
}

void MusicalStateEngine::Reset() noexcept {
  frame_ = {};
  lastSampleIndex_ = 0;
  lastHitCount_ = 0;
  lastAccentCount_ = 0;
  lastHitSampleIndex_ = 0;
  previousHitIntervalSamples_ = 0;
  eventOrdinal_ = 0;
  longTermEnergy_ = 0.0f;
  trend_ = 0.0f;
  buildupMemory_ = 0.0f;
  peakMemory_ = 0.0f;
  slowStateAgeSeconds_ = 0.0f;
  candidateAgeSeconds_ = 0.0f;
  slowState_ = SlowState::none;
  candidateState_ = SlowState::none;
  initialized_ = false;
  lastHitFlag_ = false;
  lastAccentFlag_ = false;
  haveHitSample_ = false;
}

void MusicalStateEngine::SetSampleRate(unsigned sampleRate) noexcept {
  const unsigned sanitized = sampleRate == 0 ? kDefaultSampleRate : sampleRate;
  if (sanitized == config_.sampleRate) return;
  config_.sampleRate = sanitized;
  Reset();
}

void MusicalStateEngine::Anchor(const SignalFrameV1& source) noexcept {
  const SanitizedSignal signal = Sanitize(source);
  const float immediate = ImmediateEnergy(signal);

  frame_ = {};
  frame_.sourceSampleIndex = signal.sampleIndex;
  frame_.immediateEnergy = immediate;
  frame_.sustainedEnergy = immediate;
  lastSampleIndex_ = signal.sampleIndex;
  lastHitCount_ = signal.hitCount;
  lastAccentCount_ = signal.accentCount;
  lastHitFlag_ = signal.hit;
  lastAccentFlag_ = signal.accent;
  longTermEnergy_ = immediate;
  initialized_ = true;
}

MusicalStateFrameV1 MusicalStateEngine::Advance(
    const SignalFrameV1& source) noexcept {
  if (!initialized_) {
    Anchor(source);
    return frame_;
  }

  // Duplicate analyzer publications are common when render updates outpace
  // audio blocks. Ignore every changed field until the sample clock advances.
  if (source.sampleIndex == lastSampleIndex_) return frame_;

  // A rewound source clock or either monotonic event counter moving backwards
  // denotes an analyzer restart/reconnect. Rebase cleanly, and do not turn a
  // stale true flag in the first recovered frame into a fresh musical event.
  if (source.sampleIndex < lastSampleIndex_ ||
      source.hitCount < lastHitCount_ ||
      source.accentCount < lastAccentCount_) {
    Reset();
    Anchor(source);
    return frame_;
  }

  const SanitizedSignal signal = Sanitize(source);
  const std::uint64_t deltaSamples = signal.sampleIndex - lastSampleIndex_;
  const float rawDeltaSeconds = static_cast<float>(
      static_cast<double>(deltaSamples) /
      static_cast<double>(config_.sampleRate));
  const float dt = std::min(rawDeltaSeconds, config_.maxDeltaSeconds);
  lastSampleIndex_ = signal.sampleIndex;
  frame_.sourceSampleIndex = signal.sampleIndex;
  frame_.immediateEnergy = ImmediateEnergy(signal);

  const bool hitCounterAdvanced = signal.hitCount > lastHitCount_;
  const bool accentCounterAdvanced = signal.accentCount > lastAccentCount_;
  bool onset = hitCounterAdvanced || (signal.hit && !lastHitFlag_);
  const bool accent = accentCounterAdvanced ||
      (signal.accent && !lastAccentFlag_);
  onset = onset || accent;
  lastHitCount_ = signal.hitCount;
  lastAccentCount_ = signal.accentCount;
  lastHitFlag_ = signal.hit;
  lastAccentFlag_ = signal.accent;

  // Fast interpretation: envelopes decay continuously and are raised only by
  // a new counter/rising-edge event, never by a held analyzer flag.
  frame_.onsetEnvelope = Unit(
      frame_.onsetEnvelope * std::exp(-dt / 0.30f));
  frame_.accentEnvelope = Unit(
      frame_.accentEnvelope * std::exp(-dt / 0.52f));
  if (onset) {
    frame_.onsetEnvelope = std::max(
        frame_.onsetEnvelope, 0.72f + frame_.immediateEnergy * 0.28f);
  }
  if (accent) {
    frame_.accentEnvelope = std::max(
        frame_.accentEnvelope, 0.75f + signal.highs * 0.25f);
  }

  // Medium interpretation: a first/isolated hit contributes little, while
  // repeated reasonably spaced hits accumulate a bounded groove memory.
  frame_.grooveEnergy = Unit(
      frame_.grooveEnergy * std::exp(-dt / 1.60f));
  if (onset) {
    // Strength matters as well as cadence: equal-tempo low pulses establish a
    // groove, while harder and more frequently accented hits establish a
    // stronger one without needing an unbounded hit history.
    float boost = 0.04f + frame_.immediateEnergy * 0.04f +
        (accent ? 0.04f : 0.0f);
    if (haveHitSample_) {
      const std::uint64_t intervalSamples =
          signal.sampleIndex - lastHitSampleIndex_;
      const float intervalSeconds = static_cast<float>(
          static_cast<double>(intervalSamples) /
          static_cast<double>(config_.sampleRate));
      if (intervalSeconds >= 0.16f && intervalSeconds <= 1.60f) {
        float regularity = 0.5f;
        if (previousHitIntervalSamples_ != 0) {
          const double difference = std::abs(
              static_cast<double>(intervalSamples) -
              static_cast<double>(previousHitIntervalSamples_));
          const double scale = std::max(
              static_cast<double>(intervalSamples),
              static_cast<double>(previousHitIntervalSamples_));
          regularity = Unit(1.0f - static_cast<float>(difference / scale));
        }
        boost = 0.18f + regularity * 0.13f +
            frame_.immediateEnergy * 0.10f + (accent ? 0.07f : 0.0f);
      }
      previousHitIntervalSamples_ = intervalSamples;
    }
    frame_.grooveEnergy = Unit(
        frame_.grooveEnergy + (1.0f - frame_.grooveEnergy) * boost);
    lastHitSampleIndex_ = signal.sampleIndex;
    haveHitSample_ = true;
  }

  frame_.sustainedEnergy = Follow(
      frame_.sustainedEnergy, frame_.immediateEnergy, dt, 0.45f, 2.40f);
  longTermEnergy_ = Follow(
      longTermEnergy_, frame_.sustainedEnergy, dt, 5.50f, 5.50f);
  const float trendTarget = std::clamp(
      (frame_.sustainedEnergy - longTermEnergy_) * 2.2f +
      (frame_.immediateEnergy - frame_.sustainedEnergy) * 1.2f,
      -1.0f, 1.0f);
  const float trendResponse = Unit(1.0f - std::exp(-dt / 0.70f));
  trend_ = std::clamp(
      trend_ + (trendTarget - trend_) * trendResponse, -1.0f, 1.0f);
  frame_.energyTrend = trend_;

  // Slow interpretation: buildup and peak memory make the same loud frame
  // mean something different after a rise than it does during a held section.
  const float buildStimulus =
      SmoothStep(0.03f, 0.38f, trend_) *
      SmoothStep(0.12f, 0.68f, frame_.sustainedEnergy);
  buildupMemory_ = Follow(
      buildupMemory_, buildStimulus, dt, 1.20f, 5.50f);

  const float highEnergy = SmoothStep(
      0.62f, 0.92f,
      std::max(frame_.immediateEnergy, frame_.sustainedEnergy));
  const float transient = std::max(
      frame_.onsetEnvelope, frame_.accentEnvelope * 0.90f);
  const float peakTarget = Unit(
      highEnergy * (0.10f + transient * 0.90f) *
      (0.28f + buildupMemory_ * 0.72f));
  frame_.peak = Follow(frame_.peak, peakTarget, dt, 0.08f, 0.95f);
  // A held high-energy section is not a repeated peak, but it must leave
  // enough bounded memory for its later energy loss to read as a release.
  peakMemory_ = std::max(
      Unit(peakMemory_ * std::exp(-dt / 4.50f)),
      std::max(frame_.peak, highEnergy * 0.24f));

  const float falling = SmoothStep(0.04f, 0.42f, -trend_);
  const float energyDrop = SmoothStep(
      0.08f, 0.52f,
      frame_.sustainedEnergy - frame_.immediateEnergy);
  const float releaseTarget = Unit(
      peakMemory_ * std::max(falling, energyDrop) *
      (1.0f - highEnergy * 0.35f));

  const float quiet = 1.0f - SmoothStep(
      0.08f, 0.38f, frame_.sustainedEnergy);
  const float calmTarget = Unit(
      quiet * (1.0f - frame_.onsetEnvelope * 0.60f) *
      (1.0f - frame_.grooveEnergy * 0.40f));
  const float buildingTarget = Unit(
      SmoothStep(0.12f, 0.66f, frame_.sustainedEnergy) *
      (SmoothStep(0.02f, 0.32f, trend_) * 0.62f +
       buildupMemory_ * 0.38f) *
      (1.0f - frame_.peak * 0.35f));
  const float drivingTarget = Unit(
      SmoothStep(0.28f, 0.76f, frame_.sustainedEnergy) *
      (0.55f + frame_.grooveEnergy * 0.45f));

  frame_.calm = Follow(frame_.calm, calmTarget, dt, 0.60f, 1.50f);
  frame_.building = Follow(
      frame_.building, buildingTarget, dt, 0.75f, 1.40f);
  frame_.driving = Follow(
      frame_.driving, drivingTarget, dt, 0.45f, 1.80f);
  frame_.release = Follow(
      frame_.release, releaseTarget, dt, 0.22f, 2.20f);

  UpdateSlowState(dt);
  return frame_;
}

void MusicalStateEngine::UpdateSlowState(float dt) noexcept {
  const std::array<float, 6> scores{
      0.0f,
      frame_.calm,
      frame_.building,
      frame_.driving,
      frame_.peak,
      frame_.release,
  };

  SlowState selected = SlowState::none;
  float selectedScore = 0.0f;
  for (std::size_t i = 1; i < scores.size(); ++i) {
    if (scores[i] > selectedScore) {
      selectedScore = scores[i];
      selected = static_cast<SlowState>(i);
    }
  }

  // Peak is intentionally shorter than the sustained driving confidence that
  // surrounds it. Give a strong, buildup-qualified peak a bounded priority
  // window so it can become an observable dwell-qualified event instead of
  // being masked forever by the slower driving follower. Once committed, hold
  // it only while meaningful peak confidence remains; release/driving can then
  // take over through their normal dwell rules.
  constexpr float kPeakEnterConfidence = 0.52f;
  constexpr float kPeakHoldConfidence = 0.30f;
  constexpr float kPeakPriorityMargin = 0.28f;
  const bool holdCommittedPeak = slowState_ == SlowState::peak &&
      frame_.peak >= kPeakHoldConfidence;
  const bool enterPeak = slowState_ != SlowState::peak &&
      frame_.peak >= kPeakEnterConfidence &&
      frame_.peak + kPeakPriorityMargin >= selectedScore;
  if (holdCommittedPeak || enterPeak) {
    selected = SlowState::peak;
    selectedScore = frame_.peak;
  }

  // A committed state owns a small hysteresis margin. This affects only event
  // boundaries/age; the continuous confidence fields above remain untouched.
  if (slowState_ == SlowState::none) {
    if (selectedScore < 0.42f) selected = SlowState::none;
  } else {
    const float currentScore = scores[static_cast<std::size_t>(slowState_)];
    const bool qualifiedPeakEntry = selected == SlowState::peak && enterPeak;
    if (selected != slowState_ && !qualifiedPeakEntry &&
        (selectedScore < 0.36f || selectedScore < currentScore + 0.09f)) {
      selected = slowState_;
    }
  }

  // The committed state remains current while a challenger satisfies its
  // dwell time, so its age must continue advancing until the transition
  // actually commits.
  if (slowState_ != SlowState::none) {
    slowStateAgeSeconds_ = std::min(
        config_.slowStateAgeHorizonSeconds,
        slowStateAgeSeconds_ + dt);
  }

  if (selected == slowState_) {
    candidateState_ = SlowState::none;
    candidateAgeSeconds_ = 0.0f;
  } else if (selected == SlowState::none) {
    candidateState_ = SlowState::none;
    candidateAgeSeconds_ = 0.0f;
  } else {
    if (selected != candidateState_) {
      candidateState_ = selected;
      candidateAgeSeconds_ = dt;
    } else {
      candidateAgeSeconds_ += dt;
    }

    float dwellSeconds = 0.55f;
    switch (candidateState_) {
      case SlowState::calm: dwellSeconds = 0.90f; break;
      case SlowState::building: dwellSeconds = 0.65f; break;
      case SlowState::driving: dwellSeconds = 0.55f; break;
      case SlowState::peak: dwellSeconds = 0.18f; break;
      case SlowState::release: dwellSeconds = 0.35f; break;
      case SlowState::none: break;
    }
    if (candidateAgeSeconds_ >= dwellSeconds) {
      slowState_ = candidateState_;
      candidateState_ = SlowState::none;
      candidateAgeSeconds_ = 0.0f;
      slowStateAgeSeconds_ = 0.0f;
      frame_.eventId = MakeEventId(slowState_);
    }
  }

  frame_.slowStateAge = Unit(
      slowStateAgeSeconds_ / config_.slowStateAgeHorizonSeconds);
}

std::uint64_t MusicalStateEngine::MakeEventId(SlowState state) noexcept {
  ++eventOrdinal_;
  std::uint64_t value = config_.seed ^ UINT64_C(0x9e3779b97f4a7c15);
  value ^= Mix64(eventOrdinal_ + UINT64_C(0x632be59bd9b4e019));
  value ^= Mix64(frame_.sourceSampleIndex + UINT64_C(0x8cb92ba72f3d8dd7));
  value ^= static_cast<std::uint64_t>(state) *
      UINT64_C(0xd6e8feb86659fd93);
  value = Mix64(value);
  return value == 0 ? UINT64_C(0x6a09e667f3bcc909) : value;
}

}  // namespace prismforge
