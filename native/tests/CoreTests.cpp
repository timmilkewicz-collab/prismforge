#include "PrismForge/BoundedQueue.h"
#include "PrismForge/QualityGovernor.h"
#include "PrismForge/ReactiveMotion.h"
#include "PrismForge/ShowState.h"
#include "PrismForge/SignalAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

#define CHECK(condition) do { \
  if (!(condition)) throw std::runtime_error("Check failed: " #condition); \
} while (false)

using namespace prismforge;

int main() {
  BoundedQueue<int> queue(2);
  CHECK(queue.TryPush(1));
  CHECK(queue.TryPush(2));
  CHECK(!queue.TryPush(3));
  CHECK(queue.TryPop() == 1);
  CHECK(queue.TryPop() == 2);
  CHECK(!queue.TryPop());
  BoundedQueue<int> emptyQueue(0);
  CHECK(!emptyQueue.TryPush(1));

  ShowState show;
  CHECK(show.SetScene(0, "chrome-flock"));
  CHECK(!show.SetScene(0, "unknown"));
  show.SetCrossfader(0.8f);
  CHECK(show.SaveCue(0));
  show.SetCrossfader(0.2f);
  CHECK(show.RecallCue(0, Quantization::Bar, 5));
  CHECK(!show.OnBeat(7));
  CHECK(show.Current().crossfader == 0.2f);
  CHECK(show.OnBeat(8));
  CHECK(show.Current().crossfader == 0.8f);
  ModulationRouteV1 route{"bass", "crossfader", 0.4f};
  CHECK(show.SetModulation(63, route));
  CHECK(!show.SetModulation(64, route));
  CHECK(std::abs(ShowState::ApplyModulation(0.2f, 0.5f, route) - 0.4f) < 0.001f);
  CHECK(show.SetOverlay(0, {true, "logo.png", 0.5f}));
  CHECK(!show.SetOverlay(4, {true, "logo.png", 0.5f}));
  CHECK(show.SetMasterEffect(0, 0.3f));
  ShowState restored;
  CHECK(restored.Restore(show.Current(), show.Cues(), show.Routes()));
  CHECK(restored.Current().decks[0].sceneId == "chrome-flock");
  CHECK(restored.Current().overlays[0].opacity == 0.5f);
  auto invalid = restored.Current();
  invalid.crossfader = std::numeric_limits<float>::infinity();
  CHECK(!restored.Restore(invalid, show.Cues(), show.Routes()));
  CHECK(restored.Current().crossfader == 0.8f);

  ShowState performance;
  CHECK(performance.SetScene(0, "hex-vortex"));
  CHECK(performance.SetScene(1, "ferrofluid-reactor"));
  CHECK(performance.SetMasterEffect(0, 0.35f));
  CHECK(performance.SetMasterEffect(3, 0.42f));
  CHECK(!performance.SetMasterEffect(4, 0.5f));
  CHECK(!performance.SetMasterEffect(0, std::numeric_limits<float>::quiet_NaN()));
  CHECK(performance.SaveCue(1));
  CHECK(performance.SetMasterEffect(0, 0.0f));
  CHECK(performance.RecallCue(1, Quantization::Immediate, 0));
  CHECK(performance.Current().decks[0].sceneId == "hex-vortex");
  CHECK(performance.Current().decks[1].sceneId == "ferrofluid-reactor");

  ShowState sceneControls;
  CHECK(sceneControls.SetScene(0, "mirror-cathedral"));
  CHECK(sceneControls.SetSceneParameter(0, 0, 0.82f));
  CHECK(sceneControls.SetSceneParameter(0, 3, 0.14f));
  CHECK(!sceneControls.SetSceneParameter(2, 0, 0.5f));
  CHECK(!sceneControls.SetSceneParameter(0, 4, 0.5f));
  CHECK(!sceneControls.SetSceneParameter(0, 1, -0.01f));
  CHECK(!sceneControls.SetSceneParameter(0, 1, 1.01f));
  CHECK(!sceneControls.SetSceneParameter(0, 1, std::numeric_limits<float>::quiet_NaN()));
  CHECK(sceneControls.SaveCue(0));
  CHECK(sceneControls.SetSceneParameter(0, 0, 0.22f));
  CHECK(sceneControls.RecallCue(0, Quantization::Immediate, 0));
  CHECK(sceneControls.Current().decks[0].sceneParams[0] == 0.82f);
  CHECK(sceneControls.Current().decks[0].sceneParams[3] == 0.14f);
  auto badControl = sceneControls.Current();
  badControl.decks[0].sceneParams[2] = std::numeric_limits<float>::infinity();
  CHECK(!sceneControls.Restore(badControl, sceneControls.Cues(), sceneControls.Routes()));
  CHECK(performance.Current().masterEffects[0] == 0.35f);
  CHECK(performance.Current().masterEffects[3] == 0.42f);
  CHECK(performance.SetScene(0, "shardwell"));
  CHECK(performance.SetScene(1, "neon-orbs"));
  CHECK(performance.Current().decks[0].sceneId == "shardwell");
  CHECK(performance.Current().decks[1].sceneId == "neon-orbs");
  CHECK(performance.RecallCue(1, Quantization::Immediate, 0));
  CHECK(performance.Current().decks[0].sceneId == "hex-vortex");
  CHECK(performance.Current().decks[1].sceneId == "ferrofluid-reactor");

  QualityGovernor governor;
  for (int i = 0; i < 250; ++i) governor.ObserveFrameMilliseconds(35.0);
  CHECK(governor.TierIndex() > 0);
  CHECK(governor.Current().internalScale <= 0.75f);
  for (int i = 0; i < 2500; ++i) governor.ObserveFrameMilliseconds(2.0);
  CHECK(governor.TierIndex() == 0);

  SignalAnalyzer analyzer;
  std::array<float, 256> samples{};
  for (int block = 0; block < 100; ++block) {
    for (int i = 0; i < 256; ++i) {
      const float t = static_cast<float>(block * 256 + i) / 48000.0f;
      samples[i] = 0.25f * std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * t);
    }
    analyzer.PushMono(samples.data(), samples.size());
  }
  const auto& signal = analyzer.Latest();
  const auto dominant = std::distance(signal.bands.begin(),
      std::max_element(signal.bands.begin(), signal.bands.end()));
  CHECK(dominant >= 15 && dominant <= 23);  // 1 kHz in the log-frequency map.
  CHECK(signal.rms > 0.1f && signal.rms < 0.25f);
  CHECK(signal.hitCount >= 1);

  SignalAnalyzer rhythm;
  for (int block = 0; block < 750; ++block) {  // Four seconds at 48 kHz.
    for (int i = 0; i < 256; ++i) {
      const int sampleIndex = block * 256 + i;
      const bool insideKick = sampleIndex % 24000 < 960;  // 120 BPM pulse.
      samples[i] = insideKick ? 0.8f * std::sin(
          2.0f * std::numbers::pi_v<float> * 100.0f * sampleIndex / 48000.0f) : 0.0f;
    }
    rhythm.PushMono(samples.data(), samples.size());
  }
  CHECK(rhythm.Latest().hitCount >= 7);
  CHECK(rhythm.Latest().bpm >= 118.0f && rhythm.Latest().bpm <= 122.0f);
  CHECK(rhythm.Latest().beatConfidence > 0.75f);
  samples.fill(std::numeric_limits<float>::quiet_NaN());
  rhythm.PushMono(samples.data(), samples.size());
  CHECK(std::isfinite(rhythm.Latest().rms));

  // The render-only envelope must make ordinary room-mic levels useful without
  // changing raw analyzer meters or causing a time/phase jump on audio changes.
  SignalFrameV1 reactiveSignal{};
  ReactiveMotion reactive;
  auto reactiveFrame = reactive.Advance(reactiveSignal, 0.0);
  CHECK(reactiveFrame.energy == 0.0f);
  CHECK(reactiveFrame.pulse == 0.0f);
  CHECK(reactiveFrame.palette == 0.0f);
  double reactiveTime = 0.0;
  for (int i = 0; i < 60; ++i) {
    reactiveTime += 1.0 / 60.0;
    reactiveFrame = reactive.Advance(reactiveSignal, reactiveTime);
  }
  CHECK(reactiveFrame.energy == 0.0f);
  CHECK(reactiveFrame.flow > 0.17f && reactiveFrame.flow < 0.19f);
  const float quietFlow = reactiveFrame.flow;
  reactiveSignal.rms = 0.02f;
  for (int i = 0; i < 15; ++i) {
    reactiveTime += 1.0 / 60.0;
    reactiveFrame = reactive.Advance(reactiveSignal, reactiveTime);
  }
  CHECK(reactiveFrame.energy > 0.32f && reactiveFrame.energy < 0.50f);
  CHECK(reactiveFrame.flow > quietFlow);
  const float moderateEnergy = reactiveFrame.energy;
  reactiveSignal.rms = 0.10f;
  for (int i = 0; i < 15; ++i) {
    reactiveTime += 1.0 / 60.0;
    reactiveFrame = reactive.Advance(reactiveSignal, reactiveTime);
  }
  CHECK(reactiveFrame.energy > 0.85f);
  CHECK(reactiveFrame.energy > moderateEnergy);
  reactiveSignal.rms = 0.0f;
  for (int i = 0; i < 18; ++i) {
    reactiveTime += 1.0 / 60.0;
    reactiveFrame = reactive.Advance(reactiveSignal, reactiveTime);
  }
  CHECK(reactiveFrame.energy > 0.20f && reactiveFrame.energy < 0.50f);
  for (int i = 0; i < 54; ++i) {
    reactiveTime += 1.0 / 60.0;
    reactiveFrame = reactive.Advance(reactiveSignal, reactiveTime);
  }
  CHECK(reactiveFrame.energy < 0.03f);
  CHECK(reactiveFrame.palette == 0.0f);

  ReactiveMotion strikes;
  (void)strikes.Advance({}, 0.0);
  SignalFrameV1 strikeSignal{};
  strikeSignal.rms = 0.04f;
  strikeSignal.hit = true;
  strikeSignal.hitCount = 1;
  auto strikeFrame = strikes.Advance(strikeSignal, 1.0 / 60.0);
  CHECK(strikeFrame.pulse == 1.0f);
  strikeFrame = strikes.Advance(strikeSignal, 2.0 / 60.0);
  CHECK(strikeFrame.pulse < 1.0f);  // A held flag is not a new onset.
  strikeSignal.hit = false;
  for (int i = 3; i <= 22; ++i) {
    strikeFrame = strikes.Advance(strikeSignal, i / 60.0);
  }
  CHECK(strikeFrame.pulse == 0.0f);
  strikeSignal.hitCount = 2;  // Counter edge catches a hit missed by a gate.
  strikeFrame = strikes.Advance(strikeSignal, 23.0 / 60.0);
  CHECK(strikeFrame.pulse == 1.0f);
  strikeSignal.hitCount = 0;
  strikeSignal.hit = true;
  strikeFrame = strikes.Advance(strikeSignal, 24.0 / 60.0);
  CHECK(strikeFrame.pulse < 1.0f);  // A source reset is not a new hit.

  ReactiveMotion paletteMotion;
  SignalFrameV1 musicSignal{};
  musicSignal.rms = 0.08f;
  musicSignal.hit = true;
  musicSignal.hitCount = 1;
  auto paletteFrame = paletteMotion.Advance(musicSignal, 0.0);
  CHECK(paletteFrame.palette == 0.0f);  // Transition starts without a cut.
  musicSignal.hit = false;
  double paletteTime = 0.0;
  for (int i = 0; i < 60; ++i) {
    paletteTime += 1.0 / 60.0;
    paletteFrame = paletteMotion.Advance(musicSignal, paletteTime);
  }
  CHECK(std::abs(paletteFrame.palette - 0.25f) < 0.001f);
  musicSignal.hitCount = 2;
  paletteTime += 1.0 / 60.0;
  paletteFrame = paletteMotion.Advance(musicSignal, paletteTime);
  CHECK(std::abs(paletteFrame.palette - 0.25f) < 0.001f);
  while (paletteTime < 3.2) {
    paletteTime += 1.0 / 60.0;
    paletteFrame = paletteMotion.Advance(musicSignal, paletteTime);
  }
  musicSignal.hitCount = 3;
  paletteTime += 1.0 / 60.0;
  paletteFrame = paletteMotion.Advance(musicSignal, paletteTime);
  CHECK(paletteFrame.palette > 0.25f && paletteFrame.palette < 0.26f);
  for (int i = 0; i < 60; ++i) {
    paletteTime += 1.0 / 60.0;
    paletteFrame = paletteMotion.Advance(musicSignal, paletteTime);
  }
  CHECK(std::abs(paletteFrame.palette - 0.50f) < 0.001f);

  ReactiveMotion fallback;
  SignalFrameV1 sustained{};
  sustained.rms = 0.08f;
  auto fallbackFrame = fallback.Advance(sustained, 0.0);
  for (int i = 1; i <= 474; ++i) {
    fallbackFrame = fallback.Advance(sustained, i / 60.0);
  }
  CHECK(fallbackFrame.palette == 0.0f);  // No onset before eight seconds.
  for (int i = 475; i <= 540; ++i) {
    fallbackFrame = fallback.Advance(sustained, i / 60.0);
  }
  CHECK(std::abs(fallbackFrame.palette - 0.25f) < 0.001f);

  const auto beforeRewind = fallbackFrame;
  const auto rewound = fallback.Advance(sustained, 1.0);
  CHECK(rewound.flow == beforeRewind.flow);
  CHECK(rewound.palette == beforeRewind.palette);
  const auto invalidTime = fallback.Advance(sustained,
      std::numeric_limits<double>::quiet_NaN());
  CHECK(invalidTime.flow == beforeRewind.flow);
  CHECK(fallback.Advance(sustained, -1.0).palette == beforeRewind.palette);
  sustained.rms = std::numeric_limits<float>::quiet_NaN();
  fallbackFrame = fallback.Advance(sustained, 1.0 + 1.0 / 60.0);
  CHECK(std::isfinite(fallbackFrame.energy));
  CHECK(std::isfinite(fallbackFrame.flow));
  CHECK(fallbackFrame.energy >= 0.0f && fallbackFrame.energy <= 1.0f);
  CHECK(fallbackFrame.palette >= 0.0f && fallbackFrame.palette <= 1.0f);

  std::cout << "PrismForge core tests passed\n";
}
