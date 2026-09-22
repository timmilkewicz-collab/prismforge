#include "PrismForge/QualityGovernor.h"

#include <cmath>

namespace prismforge {

void QualityGovernor::ObserveFrameMilliseconds(double frameMs) {
  if (!std::isfinite(frameMs) || frameMs <= 0.0) return;
  smoothedMs_ = smoothedMs_ * 0.94 + frameMs * 0.06;
  const double budget = 1000.0 / kTiers[tier_].targetFps;
  if (smoothedMs_ > budget * 1.12) {
    ++overloadFrames_;
    healthyFrames_ = 0;
    if (overloadFrames_ >= 45 && tier_ + 1 < 4) {
      ++tier_;
      overloadFrames_ = 0;
    }
  } else if (smoothedMs_ < budget * 0.72) {
    ++healthyFrames_;
    overloadFrames_ = 0;
    if (healthyFrames_ >= 600 && tier_ > 0) {
      --tier_;
      healthyFrames_ = 0;
    }
  } else {
    overloadFrames_ = 0;
    healthyFrames_ = 0;
  }
}

}  // namespace prismforge
