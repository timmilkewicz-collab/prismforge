#include "SignalTelemetry.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

using namespace prismforge;

namespace {
int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                          \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition      \
                << '\n';                                                         \
      ++failures;                                                                \
    }                                                                            \
  } while (false)

bool Near(double actual, double expected, double tolerance = 1.0e-6) {
  return std::abs(actual - expected) <= tolerance;
}
}  // namespace

int main() {
  SignalFrameV1 signal{};
  signal.sampleIndex = 144000;
  signal.rms = 0.24f;
  signal.peak = 0.72f;
  signal.bass = 0.31f;
  signal.mids = 0.42f;
  signal.highs = 0.53f;
  signal.hit = true;
  signal.accent = true;
  signal.hitCount = 17;
  signal.accentCount = 5;
  signal.bpm = 140.0f;
  signal.beatPhase = 0.25f;
  signal.beatConfidence = 0.8f;

  MusicalStateFrameV1 musical{};
  musical.sourceSampleIndex = 96000;
  musical.eventId = std::numeric_limits<std::uint64_t>::max();
  musical.immediateEnergy = 0.20f;
  musical.onsetEnvelope = 0.30f;
  musical.accentEnvelope = 0.40f;
  musical.grooveEnergy = 0.50f;
  musical.sustainedEnergy = 0.60f;
  musical.energyTrend = -0.20f;
  musical.calm = 0.70f;
  musical.building = 0.35f;
  musical.driving = 0.45f;
  musical.peak = 0.80f;
  musical.release = 0.65f;
  musical.slowStateAge = 0.10f;

  const auto payload = BuildSignalTelemetry(
      signal, musical, 48000, 59.94, 1.25, 60, 2, 0.33f, "swipe_left");

  // Existing raw analyzer and cadence fields remain intact.
  CHECK(payload.at("sampleIndex") == 144000);
  CHECK(payload.at("hitCount") == 17);
  CHECK(payload.at("accentCount") == 5);
  CHECK(Near(payload.at("audio").at("rms").get<double>(), 0.24));
  CHECK(Near(payload.at("performance").at("fps").get<double>(), 59.94));
  CHECK(Near(payload.at("performance").at("frameTimeMs").get<double>(), 1.25));
  CHECK(payload.at("performance").at("targetFps") == 60);

  const auto& observed = payload.at("musical");
  CHECK(observed.at("sourceSampleIndex") == 96000);
  CHECK(observed.at("sampleRate") == 48000);
  CHECK(Near(observed.at("sampleTimeSeconds").get<double>(), 2.0));
  CHECK(observed.at("eventId") == "18446744073709551615");
  CHECK(Near(observed.at("grooveEnergy").get<double>(), 0.50));
  CHECK(Near(observed.at("calm").get<double>(), 0.70));
  CHECK(Near(observed.at("building").get<double>(), 0.35));
  CHECK(Near(observed.at("driving").get<double>(), 0.45));
  CHECK(Near(observed.at("peak").get<double>(), 0.80));
  CHECK(Near(observed.at("release").get<double>(), 0.65));

  // A malformed zero rate uses the same deterministic default as the engine.
  const auto defaultRatePayload = BuildSignalTelemetry(
      signal, musical, 0, 0.0, 0.0, 60, 0, 0.0f, "");
  CHECK(defaultRatePayload.at("musical").at("sampleRate") == 48000);
  CHECK(Near(defaultRatePayload.at("musical").at("sampleTimeSeconds")
                 .get<double>(), 2.0));

  if (failures != 0) return 1;
  std::cout << "Signal telemetry tests passed\n";
  return 0;
}
