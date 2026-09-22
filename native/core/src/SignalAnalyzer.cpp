#include "PrismForge/SignalAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace prismforge {
namespace {
constexpr float kSmall = 1.0e-8f;

void Fft(std::array<std::complex<float>, SignalAnalyzer::kFftSize>& values) {
  constexpr std::size_t n = SignalAnalyzer::kFftSize;
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(values[i], values[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const float angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
    const std::complex<float> root(std::cos(angle), std::sin(angle));
    for (std::size_t start = 0; start < n; start += len) {
      std::complex<float> rotation(1.0f, 0.0f);
      for (std::size_t j = 0; j < len / 2; ++j) {
        const auto even = values[start + j];
        const auto odd = values[start + j + len / 2] * rotation;
        values[start + j] = even + odd;
        values[start + j + len / 2] = even - odd;
        rotation *= root;
      }
    }
  }
}
}  // namespace

SignalAnalyzer::SignalAnalyzer(unsigned sampleRate)
    : sampleRate_(sampleRate == 0 ? 48000 : sampleRate) {}

void SignalAnalyzer::PushMono(const float* samples, std::size_t count) {
  if (!samples) return;
  for (std::size_t i = 0; i < count; ++i) {
    const float sample = std::isfinite(samples[i]) ? std::clamp(samples[i], -1.0f, 1.0f) : 0.0f;
    history_[historyPos_] = sample;
    historyPos_ = (historyPos_ + 1) % kFftSize;
    historyFilled_ = std::min(historyFilled_ + 1, kFftSize);
    ++sinceFft_;
    ++frame_.sampleIndex;
    transientSquares_ += static_cast<double>(sample) * sample;
    transientPeak_ = std::max(transientPeak_, std::abs(sample));
    if (++transientCount_ == kTransientWindow) FinishTransientWindow();
    if (historyFilled_ == kFftSize && sinceFft_ >= kHopSize) {
      sinceFft_ = 0;
      CalculateSpectrum();
    }
  }
}

void SignalAnalyzer::FinishTransientWindow() {
  const float rms = static_cast<float>(std::sqrt(transientSquares_ / kTransientWindow));
  frame_.rms = rms;
  frame_.peak = transientPeak_;
  frame_.hit = false;
  frame_.accent = false;

  const std::uint64_t refractory = sampleRate_ / 10;
  const bool ready = !hadHit_ || frame_.sampleIndex - lastHitSample_ >= refractory;
  if (ready && rms > std::max(0.025f, floor_ * 1.65f)) {
    frame_.hit = true;
    frame_.accent = rms > std::max(0.07f, floor_ * 2.35f);
    ++frame_.hitCount;
    if (frame_.accent) ++frame_.accentCount;
    if (hadHit_) {
      const std::uint64_t interval = frame_.sampleIndex - lastHitSample_;
      const float seconds = static_cast<float>(interval) / static_cast<float>(sampleRate_);
      if (seconds >= 0.25f && seconds <= 2.0f) {
        frame_.bpm = 60.0f / seconds;
        if (lastBeatIntervalSamples_ != 0) {
          const float ratio = static_cast<float>(interval) /
              static_cast<float>(lastBeatIntervalSamples_);
          beatConfidence_ = std::clamp(1.0f - std::abs(1.0f - ratio) * 2.5f, 0.0f, 1.0f);
        } else {
          beatConfidence_ = 0.35f;
        }
        lastBeatIntervalSamples_ = interval;
      } else {
        beatConfidence_ *= 0.5f;
      }
    }
    previousHitSample_ = lastHitSample_;
    lastHitSample_ = frame_.sampleIndex;
    hadHit_ = true;
  }

  const float interval = frame_.bpm > 0.0f ?
      60.0f * sampleRate_ / frame_.bpm : static_cast<float>(sampleRate_);
  frame_.beatPhase = hadHit_ ? std::fmod(
      static_cast<float>(frame_.sampleIndex - lastHitSample_), interval) / interval : 0.0f;
  frame_.beatConfidence = beatConfidence_;
  floor_ = 0.988f * floor_ + 0.012f * rms;
  transientCount_ = 0;
  transientSquares_ = 0.0;
  transientPeak_ = 0.0f;
}

void SignalAnalyzer::CalculateSpectrum() {
  std::array<std::complex<float>, kFftSize> spectrum{};
  for (std::size_t i = 0; i < kFftSize; ++i) {
    const float window = 0.5f - 0.5f * std::cos(
        2.0f * std::numbers::pi_v<float> * static_cast<float>(i) /
        static_cast<float>(kFftSize - 1));
    spectrum[i] = history_[(historyPos_ + i) % kFftSize] * window;
  }
  Fft(spectrum);

  constexpr float lowFrequency = 25.0f;
  const float highFrequency = std::min(18000.0f, sampleRate_ * 0.48f);
  for (std::size_t band = 0; band < frame_.bands.size(); ++band) {
    const float low = lowFrequency * std::pow(highFrequency / lowFrequency,
        static_cast<float>(band) / frame_.bands.size());
    const float high = lowFrequency * std::pow(highFrequency / lowFrequency,
        static_cast<float>(band + 1) / frame_.bands.size());
    const auto first = static_cast<std::size_t>(std::clamp(
        std::ceil(static_cast<double>(low) * kFftSize / sampleRate_),
        1.0, static_cast<double>(kFftSize / 2 - 1)));
    const auto last = static_cast<std::size_t>(std::clamp(
        std::floor(static_cast<double>(high) * kFftSize / sampleRate_),
        static_cast<double>(first),
        static_cast<double>(kFftSize / 2 - 1)));
    float power = 0.0f;
    for (std::size_t bin = first; bin <= last; ++bin) {
      power += std::norm(spectrum[bin]);
    }
    const float measured = std::sqrt(power / (last - first + 1)) * (4.0f / kFftSize);
    frame_.bands[band] = frame_.bands[band] * 0.55f +
        std::clamp(measured, 0.0f, 1.0f) * 0.45f;
  }
  const auto average = [&](std::size_t first, std::size_t last) {
    float sum = 0.0f;
    for (std::size_t i = first; i < last; ++i) sum += frame_.bands[i];
    return sum / static_cast<float>(last - first);
  };
  frame_.bass = average(0, 11);
  frame_.mids = average(11, 23);
  frame_.highs = average(23, 32);
}

}  // namespace prismforge
