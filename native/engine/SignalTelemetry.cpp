#include "SignalTelemetry.h"

#include <cstdint>
#include <string>

namespace prismforge {

nlohmann::json BuildSignalTelemetry(
    const SignalFrameV1& signal,
    const MusicalStateFrameV1& musical,
    unsigned sampleRate,
    double fps,
    double frameMs,
    unsigned targetFps,
    unsigned tier,
    float gestureMotion,
    const std::string& gestureName) {
  constexpr unsigned kDefaultSampleRate = 48000;
  const unsigned safeSampleRate = sampleRate == 0 ? kDefaultSampleRate : sampleRate;
  const double musicalSampleTimeSeconds =
      static_cast<double>(musical.sourceSampleIndex) /
      static_cast<double>(safeSampleRate);

  return {
      {"sampleIndex", signal.sampleIndex}, {"rms", signal.rms},
      {"peak", signal.peak}, {"bands", signal.bands},
      {"low", signal.bass}, {"mid", signal.mids}, {"high", signal.highs},
      {"hit", signal.hit}, {"accent", signal.accent},
      {"hitCount", signal.hitCount}, {"accentCount", signal.accentCount},
      {"bpm", signal.bpm}, {"beatPhase", signal.beatPhase},
      {"beatConfidence", signal.beatConfidence},
      {"gesture", {{"motion", gestureMotion}, {"name", gestureName}}},
      {"audio", {{"rms", signal.rms}, {"peak", signal.peak},
                  {"low", signal.bass}, {"mid", signal.mids},
                  {"high", signal.highs}, {"clipping", signal.peak >= 0.99f}}},
      {"musical", {
          {"sourceSampleIndex", musical.sourceSampleIndex},
          {"sampleRate", safeSampleRate},
          {"sampleTimeSeconds", musicalSampleTimeSeconds},
          // JSON/JavaScript numbers cannot represent arbitrary uint64 values
          // exactly. Keep the stable ID inspectable as a decimal string.
          {"eventId", std::to_string(musical.eventId)},
          {"immediateEnergy", musical.immediateEnergy},
          {"onsetEnvelope", musical.onsetEnvelope},
          {"accentEnvelope", musical.accentEnvelope},
          {"grooveEnergy", musical.grooveEnergy},
          {"sustainedEnergy", musical.sustainedEnergy},
          {"energyTrend", musical.energyTrend},
          {"calm", musical.calm},
          {"building", musical.building},
          {"driving", musical.driving},
          {"peak", musical.peak},
          {"release", musical.release},
          {"slowStateAge", musical.slowStateAge}}},
      {"performance", {{"fps", fps}, {"targetFps", targetFps},
                       {"frameTimeMs", frameMs}, {"adaptiveQuality", tier}}}};
}

}  // namespace prismforge
