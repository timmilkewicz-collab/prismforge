#pragma once

namespace prismforge {

struct QualityTier {
  float internalScale;
  unsigned targetFps;
};

class QualityGovernor {
 public:
  static constexpr QualityTier kTiers[4] = {
      {1.0f, 60}, {0.75f, 60}, {0.66f, 60}, {2.0f / 3.0f, 30}};

  void ObserveFrameMilliseconds(double frameMs);
  [[nodiscard]] unsigned TierIndex() const noexcept { return tier_; }
  [[nodiscard]] QualityTier Current() const noexcept { return kTiers[tier_]; }
  [[nodiscard]] double SmoothedFrameMs() const noexcept { return smoothedMs_; }

 private:
  unsigned tier_ = 0;
  double smoothedMs_ = 16.67;
  unsigned overloadFrames_ = 0;
  unsigned healthyFrames_ = 0;
};

}  // namespace prismforge
