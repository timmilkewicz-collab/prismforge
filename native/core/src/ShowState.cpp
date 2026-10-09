#include "PrismForge/ShowState.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace prismforge {
namespace {
bool ValidSnapshot(const ShowSnapshot& snapshot) {
  if (!std::isfinite(snapshot.crossfader) || snapshot.crossfader < 0.0f ||
      snapshot.crossfader > 1.0f) return false;
  for (const auto& deck : snapshot.decks) {
    if (std::find(kSceneIds.begin(), kSceneIds.end(), deck.sceneId) == kSceneIds.end()) {
      return false;
    }
    for (float amount : deck.effects) {
      if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f) return false;
    }
    for (float amount : deck.sceneParams) {
      if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f) return false;
    }
  }
  for (const auto& overlay : snapshot.overlays) {
    if (!std::isfinite(overlay.opacity) || overlay.opacity < 0.0f ||
        overlay.opacity > 1.0f) return false;
  }
  for (float amount : snapshot.masterEffects) {
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f) return false;
  }
  return true;
}
}  // namespace

ShowState::ShowState() {
  current_.decks[1].sceneId = "prism-atrium";
}

bool ShowState::SetScene(unsigned deck, std::string_view sceneId) {
  if (deck >= current_.decks.size() ||
      std::find(kSceneIds.begin(), kSceneIds.end(), sceneId) == kSceneIds.end()) {
    return false;
  }
  current_.decks[deck].sceneId = sceneId;
  return true;
}

void ShowState::SetCrossfader(float value) {
  if (std::isfinite(value)) current_.crossfader = std::clamp(value, 0.0f, 1.0f);
}

void ShowState::SetEffect(unsigned deck, unsigned index, float value) {
  if (deck < current_.decks.size() && index < 4 && std::isfinite(value)) {
    current_.decks[deck].effects[index] = std::clamp(value, 0.0f, 1.0f);
  }
}

bool ShowState::SetSceneParameter(unsigned deck, unsigned index, float value) {
  if (deck >= current_.decks.size() ||
      index >= current_.decks[deck].sceneParams.size() ||
      !std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
  current_.decks[deck].sceneParams[index] = value;
  return true;
}

bool ShowState::SetOverlay(unsigned slot, OverlayState overlay) {
  if (slot >= current_.overlays.size() || !std::isfinite(overlay.opacity) ||
      overlay.opacity < 0.0f || overlay.opacity > 1.0f) return false;
  current_.overlays[slot] = std::move(overlay);
  return true;
}

bool ShowState::SetMasterEffect(unsigned index, float value) {
  if (index >= current_.masterEffects.size() || !std::isfinite(value)) return false;
  current_.masterEffects[index] = std::clamp(value, 0.0f, 1.0f);
  return true;
}

bool ShowState::SaveCue(unsigned index) {
  if (index >= cues_.size()) return false;
  cues_[index] = current_;
  return true;
}

bool ShowState::RecallCue(unsigned index, Quantization quantization,
                          std::uint64_t beatCount) {
  if (index >= cues_.size() || !cues_[index]) return false;
  if (quantization == Quantization::Immediate) {
    current_ = *cues_[index];
    pendingCue_.reset();
  } else {
    const auto due = quantization == Quantization::Beat ? beatCount + 1 :
        ((beatCount / 4) + 1) * 4;
    pendingCue_ = PendingCue{index, due};
  }
  return true;
}

bool ShowState::OnBeat(std::uint64_t beatCount) {
  if (pendingCue_ && beatCount >= pendingCue_->dueBeat) {
    current_ = *cues_[pendingCue_->index];
    pendingCue_.reset();
    return true;
  }
  return false;
}

bool ShowState::SetModulation(unsigned slot, ModulationRouteV1 route) {
  if (slot >= kMaxModulationRoutes || route.source.empty() || route.target.empty() ||
      !std::isfinite(route.amount) || !std::isfinite(route.curve) ||
      !std::isfinite(route.minValue) || !std::isfinite(route.maxValue) ||
      route.minValue > route.maxValue) return false;
  if (routes_.size() <= slot) {
    const auto oldSize = routes_.size();
    routes_.resize(slot + 1);
    for (auto index = oldSize; index < routes_.size(); ++index) {
      routes_[index].enabled = false;
    }
  }
  routes_[slot] = std::move(route);
  return true;
}

bool ShowState::Restore(ShowSnapshot current, CueBank cues,
                        std::vector<ModulationRouteV1> routes) {
  if (!ValidSnapshot(current) || routes.size() > kMaxModulationRoutes) return false;
  for (const auto& cue : cues) {
    if (cue && !ValidSnapshot(*cue)) return false;
  }
  for (const auto& route : routes) {
    if (route.source.empty() && route.target.empty() && !route.enabled) continue;
    if (route.source.empty() || route.target.empty() ||
        !std::isfinite(route.amount) || !std::isfinite(route.polarity) ||
        !std::isfinite(route.curve) || !std::isfinite(route.attackMs) ||
        !std::isfinite(route.releaseMs) || !std::isfinite(route.deadband) ||
        !std::isfinite(route.minValue) || !std::isfinite(route.maxValue) ||
        route.minValue > route.maxValue) return false;
  }
  current_ = std::move(current);
  cues_ = std::move(cues);
  routes_ = std::move(routes);
  pendingCue_.reset();
  return true;
}

float ShowState::ApplyModulation(float base, float signal,
                                 const ModulationRouteV1& route) {
  if (!route.enabled || !std::isfinite(base) || !std::isfinite(signal)) return base;
  const float input = std::clamp(signal, 0.0f, 1.0f);
  if (input < std::clamp(route.deadband, 0.0f, 1.0f)) return base;
  const float shaped = std::pow(input, std::max(0.01f, route.curve));
  const float value = base + shaped * route.amount * route.polarity;
  return std::clamp(value, route.minValue, route.maxValue);
}

}  // namespace prismforge
