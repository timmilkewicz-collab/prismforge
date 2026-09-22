#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace prismforge {

struct SignalFrameV1 {
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

class SignalAnalyzer {
 public:
  static constexpr std::size_t kTransientWindow = 256;
  static constexpr std::size_t kFftSize = 2048;
  static constexpr std::size_t kHopSize = 512;

  explicit SignalAnalyzer(unsigned sampleRate = 48000);
  void PushMono(const float* samples, std::size_t count);
  [[nodiscard]] const SignalFrameV1& Latest() const noexcept { return frame_; }
  [[nodiscard]] unsigned SampleRate() const noexcept { return sampleRate_; }

 private:
  void FinishTransientWindow();
  void CalculateSpectrum();

  unsigned sampleRate_;
  SignalFrameV1 frame_{};
  std::array<float, kFftSize> history_{};
  std::size_t historyPos_ = 0;
  std::size_t historyFilled_ = 0;
  std::size_t sinceFft_ = 0;
  std::size_t transientCount_ = 0;
  double transientSquares_ = 0.0;
  float transientPeak_ = 0.0f;
  float floor_ = 0.01f;
  std::uint64_t lastHitSample_ = 0;
  std::uint64_t previousHitSample_ = 0;
  bool hadHit_ = false;
  std::uint64_t lastBeatIntervalSamples_ = 0;
  float beatConfidence_ = 0.0f;
};

}  // namespace prismforge
