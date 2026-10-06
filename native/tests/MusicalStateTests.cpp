#include "PrismForge/MusicalStateEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef PRISMFORGE_MUSICAL_FIXTURE_DIR
#error PRISMFORGE_MUSICAL_FIXTURE_DIR must name the checked-in fixture directory
#endif

namespace {

using prismforge::MusicalStateEngine;
using prismforge::MusicalStateEngineConfig;
using prismforge::MusicalStateFrameV1;
using prismforge::SignalFrameV1;

constexpr std::uint64_t kReplaySeed = 0x6d75736963616c31ULL;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr double kHashQuantization = 1000000.0;

void Require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

std::vector<std::string> SplitCsv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  for (;;) {
    const std::size_t comma = line.find(',', begin);
    fields.emplace_back(line.substr(begin, comma - begin));
    if (comma == std::string::npos) break;
    begin = comma + 1;
  }
  return fields;
}

std::uint64_t ParseU64(const std::string& text, const std::filesystem::path& path,
                       std::size_t lineNumber) {
  std::size_t consumed = 0;
  try {
    const std::uint64_t value = std::stoull(text, &consumed);
    if (consumed != text.size()) throw std::invalid_argument("trailing data");
    return value;
  } catch (const std::exception&) {
    std::ostringstream message;
    message << path.string() << ':' << lineNumber << ": invalid uint64 '"
            << text << '\'';
    throw std::runtime_error(message.str());
  }
}

float ParseFloat(const std::string& text, const std::filesystem::path& path,
                 std::size_t lineNumber) {
  std::size_t consumed = 0;
  try {
    const float value = std::stof(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value)) {
      throw std::invalid_argument("invalid finite float");
    }
    return value;
  } catch (const std::exception&) {
    std::ostringstream message;
    message << path.string() << ':' << lineNumber << ": invalid float '"
            << text << '\'';
    throw std::runtime_error(message.str());
  }
}

bool ParseBool(const std::string& text, const std::filesystem::path& path,
               std::size_t lineNumber) {
  if (text == "0") return false;
  if (text == "1") return true;
  std::ostringstream message;
  message << path.string() << ':' << lineNumber << ": invalid boolean '"
          << text << '\'';
  throw std::runtime_error(message.str());
}

std::vector<std::string> ExpectedColumns() {
  std::vector<std::string> columns{"sampleIndex", "rms", "peak"};
  for (std::size_t band = 0; band < 32; ++band) {
    std::ostringstream name;
    name << "band" << std::setw(2) << std::setfill('0') << band;
    columns.push_back(name.str());
  }
  const std::array tail{
      "bass", "mids", "highs", "hit", "accent", "hitCount",
      "accentCount", "bpm", "beatPhase", "beatConfidence"};
  columns.insert(columns.end(), tail.begin(), tail.end());
  return columns;
}

std::vector<SignalFrameV1> LoadFixture(std::string_view fileName) {
  const std::filesystem::path path =
      std::filesystem::path(PRISMFORGE_MUSICAL_FIXTURE_DIR) / fileName;
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to open musical fixture: " + path.string());
  }

  std::string line;
  Require(static_cast<bool>(std::getline(input, line)), "Fixture has no header");
  if (!line.empty() && line.back() == '\r') line.pop_back();
  const auto expected = ExpectedColumns();
  const auto actual = SplitCsv(line);
  if (actual != expected) {
    throw std::runtime_error("Fixture schema mismatch: " + path.string());
  }

  std::vector<SignalFrameV1> frames;
  std::size_t lineNumber = 1;
  while (std::getline(input, line)) {
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const auto fields = SplitCsv(line);
    if (fields.size() != expected.size()) {
      std::ostringstream message;
      message << path.string() << ':' << lineNumber << ": expected "
              << expected.size() << " columns, got " << fields.size();
      throw std::runtime_error(message.str());
    }

    SignalFrameV1 frame{};
    std::size_t column = 0;
    frame.sampleIndex = ParseU64(fields[column++], path, lineNumber);
    frame.rms = ParseFloat(fields[column++], path, lineNumber);
    frame.peak = ParseFloat(fields[column++], path, lineNumber);
    for (float& band : frame.bands) {
      band = ParseFloat(fields[column++], path, lineNumber);
    }
    frame.bass = ParseFloat(fields[column++], path, lineNumber);
    frame.mids = ParseFloat(fields[column++], path, lineNumber);
    frame.highs = ParseFloat(fields[column++], path, lineNumber);
    frame.hit = ParseBool(fields[column++], path, lineNumber);
    frame.accent = ParseBool(fields[column++], path, lineNumber);
    frame.hitCount = ParseU64(fields[column++], path, lineNumber);
    frame.accentCount = ParseU64(fields[column++], path, lineNumber);
    frame.bpm = ParseFloat(fields[column++], path, lineNumber);
    frame.beatPhase = ParseFloat(fields[column++], path, lineNumber);
    frame.beatConfidence = ParseFloat(fields[column++], path, lineNumber);
    Require(column == fields.size(), "Fixture parser did not consume each column");
    if (!frames.empty()) {
      Require(frame.sampleIndex > frames.back().sampleIndex,
              "Fixture sample indices must be strictly increasing");
      Require(frame.hitCount >= frames.back().hitCount,
              "Fixture hit counter must be monotonic");
      Require(frame.accentCount >= frames.back().accentCount,
              "Fixture accent counter must be monotonic");
    }
    frames.push_back(frame);
  }
  Require(!frames.empty(), "Fixture must contain at least one feature frame");
  return frames;
}

bool ExactFieldsEqual(const MusicalStateFrameV1& left,
                      const MusicalStateFrameV1& right) {
  return left.sourceSampleIndex == right.sourceSampleIndex &&
      left.eventId == right.eventId &&
      left.immediateEnergy == right.immediateEnergy &&
      left.onsetEnvelope == right.onsetEnvelope &&
      left.accentEnvelope == right.accentEnvelope &&
      left.grooveEnergy == right.grooveEnergy &&
      left.sustainedEnergy == right.sustainedEnergy &&
      left.energyTrend == right.energyTrend && left.calm == right.calm &&
      left.building == right.building && left.driving == right.driving &&
      left.peak == right.peak && left.release == right.release &&
      left.slowStateAge == right.slowStateAge;
}

bool ExactFieldsExceptEventEqual(const MusicalStateFrameV1& left,
                                 const MusicalStateFrameV1& right) {
  MusicalStateFrameV1 normalizedLeft = left;
  MusicalStateFrameV1 normalizedRight = right;
  normalizedLeft.eventId = 0;
  normalizedRight.eventId = 0;
  return ExactFieldsEqual(normalizedLeft, normalizedRight);
}

bool ExactFieldsExceptSourceEqual(const MusicalStateFrameV1& left,
                                  const MusicalStateFrameV1& right) {
  MusicalStateFrameV1 normalizedLeft = left;
  MusicalStateFrameV1 normalizedRight = right;
  normalizedLeft.sourceSampleIndex = 0;
  normalizedRight.sourceSampleIndex = 0;
  return ExactFieldsEqual(normalizedLeft, normalizedRight);
}

void RequireFiniteAndBounded(const MusicalStateFrameV1& frame) {
  const std::array normalized{
      frame.immediateEnergy, frame.onsetEnvelope, frame.accentEnvelope,
      frame.grooveEnergy, frame.sustainedEnergy, frame.calm, frame.building,
      frame.driving, frame.peak, frame.release, frame.slowStateAge};
  for (float value : normalized) {
    Require(std::isfinite(value), "Musical state contains a non-finite value");
    Require(value >= 0.0f && value <= 1.0f,
            "Normalized musical state escaped [0, 1]");
  }
  Require(std::isfinite(frame.energyTrend), "Energy trend is non-finite");
  Require(frame.energyTrend >= -1.0f && frame.energyTrend <= 1.0f,
          "Energy trend escaped [-1, 1]");
}

void HashByte(std::uint64_t& hash, std::uint8_t value) {
  hash ^= value;
  hash *= kFnvPrime;
}

// Canonical replay digest: unsigned and signed values are serialized in the
// explicit field order below, least-significant byte first. Floats are rounded
// to signed millionths. This intentionally never observes struct bytes/padding.
void HashU64(std::uint64_t& hash, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    HashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

void HashFloat(std::uint64_t& hash, float value) {
  const auto quantized = static_cast<std::int64_t>(
      std::llround(static_cast<double>(value) * kHashQuantization));
  HashU64(hash, static_cast<std::uint64_t>(quantized));
}

std::uint64_t ReplayHash(const std::vector<MusicalStateFrameV1>& frames) {
  std::uint64_t hash = kFnvOffset;
  HashU64(hash, static_cast<std::uint64_t>(frames.size()));
  for (const auto& frame : frames) {
    HashU64(hash, frame.sourceSampleIndex);
    HashU64(hash, frame.eventId);
    HashFloat(hash, frame.immediateEnergy);
    HashFloat(hash, frame.onsetEnvelope);
    HashFloat(hash, frame.accentEnvelope);
    HashFloat(hash, frame.grooveEnergy);
    HashFloat(hash, frame.sustainedEnergy);
    HashFloat(hash, frame.energyTrend);
    HashFloat(hash, frame.calm);
    HashFloat(hash, frame.building);
    HashFloat(hash, frame.driving);
    HashFloat(hash, frame.peak);
    HashFloat(hash, frame.release);
    HashFloat(hash, frame.slowStateAge);
  }
  return hash;
}

struct FixtureSpec {
  const char* name;
  const char* fileName;
  std::size_t expectedFrames;
  std::uint64_t goldenHash;
};

struct FixtureReplay {
  FixtureSpec spec{};
  std::vector<SignalFrameV1> signals;
  std::vector<MusicalStateFrameV1> states;
};

FixtureReplay ReplayFixture(const FixtureSpec& spec) {
  FixtureReplay replay;
  replay.spec = spec;
  replay.signals = LoadFixture(spec.fileName);
  Require(replay.signals.size() == spec.expectedFrames,
          std::string(spec.name) + " fixture frame-count changed");

  MusicalStateEngineConfig config{};
  config.seed = kReplaySeed;
  MusicalStateEngine first(config);
  MusicalStateEngine second(config);
  replay.states.reserve(replay.signals.size());
  for (const auto& signal : replay.signals) {
    const auto firstFrame = first.Advance(signal);
    const auto secondFrame = second.Advance(signal);
    Require(ExactFieldsEqual(firstFrame, secondFrame),
            std::string(spec.name) + " differed across fresh engines");
    if (!replay.states.empty() && firstFrame.eventId != 0 &&
        firstFrame.eventId == replay.states.back().eventId) {
      const float previousAge = replay.states.back().slowStateAge;
      Require(firstFrame.slowStateAge >= previousAge &&
                  (previousAge >= 1.0f ||
                   firstFrame.slowStateAge > previousAge),
              std::string(spec.name) +
                  " committed slow-state age paused or moved backwards");
    }
    Require(firstFrame.sourceSampleIndex == signal.sampleIndex,
            std::string(spec.name) + " lost source sample identity");
    RequireFiniteAndBounded(firstFrame);
    replay.states.push_back(firstFrame);
  }

  first.Reset();
  for (std::size_t index = 0; index < replay.signals.size(); ++index) {
    const auto afterReset = first.Advance(replay.signals[index]);
    Require(ExactFieldsEqual(afterReset, replay.states[index]),
            std::string(spec.name) + " reset replay was not exact");
  }

  const std::uint64_t actualHash = ReplayHash(replay.states);
  if (actualHash != spec.goldenHash) {
    std::ostringstream message;
    message << spec.name << " replay hash mismatch: expected 0x" << std::hex
            << std::setw(16) << std::setfill('0') << spec.goldenHash
            << ", got 0x" << std::setw(16) << actualHash;
    throw std::runtime_error(message.str());
  }
  return replay;
}

float RangeMax(const std::vector<MusicalStateFrameV1>& frames,
               std::size_t begin, std::size_t end,
               float MusicalStateFrameV1::*field) {
  Require(begin < end && end <= frames.size(), "Invalid semantic range");
  float result = frames[begin].*field;
  for (std::size_t index = begin + 1; index < end; ++index) {
    result = std::max(result, frames[index].*field);
  }
  return result;
}

float RangeMean(const std::vector<MusicalStateFrameV1>& frames,
                std::size_t begin, std::size_t end,
                float MusicalStateFrameV1::*field) {
  Require(begin < end && end <= frames.size(), "Invalid semantic range");
  double sum = 0.0;
  for (std::size_t index = begin; index < end; ++index) {
    sum += frames[index].*field;
  }
  return static_cast<float>(sum / static_cast<double>(end - begin));
}

float RangeMin(const std::vector<MusicalStateFrameV1>& frames,
               std::size_t begin, std::size_t end,
               float MusicalStateFrameV1::*field) {
  Require(begin < end && end <= frames.size(), "Invalid semantic range");
  float result = frames[begin].*field;
  for (std::size_t index = begin + 1; index < end; ++index) {
    result = std::min(result, frames[index].*field);
  }
  return result;
}

std::size_t CountEventTransitions(
    const std::vector<MusicalStateFrameV1>& frames) {
  std::size_t transitions = 0;
  std::uint64_t previous = 0;
  for (const auto& frame : frames) {
    if (frame.eventId != 0 && frame.eventId != previous) ++transitions;
    previous = frame.eventId;
  }
  return transitions;
}

std::size_t FirstIndexAbove(
    const std::vector<MusicalStateFrameV1>& frames, std::size_t begin,
    std::size_t end, float MusicalStateFrameV1::*field, float threshold) {
  Require(begin < end && end <= frames.size(), "Invalid semantic range");
  for (std::size_t index = begin; index < end; ++index) {
    if (frames[index].*field > threshold) return index;
  }
  return frames.size();
}

bool HasDifferentEvent(const std::vector<MusicalStateFrameV1>& frames,
                       std::size_t begin, std::size_t end,
                       std::uint64_t reference) {
  Require(begin < end && end <= frames.size(), "Invalid semantic range");
  for (std::size_t index = begin; index < end; ++index) {
    if (frames[index].eventId != 0 && frames[index].eventId != reference) {
      return true;
    }
  }
  return false;
}

void VerifyFixtureSemantics(const std::array<FixtureReplay, 8>& replay) {
  const auto& silence = replay[0].states;
  Require(RangeMean(silence, 180, 240, &MusicalStateFrameV1::calm) > 0.75f,
          "Silence did not settle into calm");
  Require(RangeMean(silence, 180, 240, &MusicalStateFrameV1::calm) >
              RangeMean(silence, 0, 60, &MusicalStateFrameV1::calm) + 0.45f,
          "Calm did not rise progressively during silence");
  Require(RangeMax(silence, 180, 240,
                   &MusicalStateFrameV1::immediateEnergy) < 0.03f,
          "Silence retained immediate energy");
  Require(RangeMax(silence, 0, silence.size(),
                   &MusicalStateFrameV1::peak) < 0.001f &&
              RangeMax(silence, 0, silence.size(),
                       &MusicalStateFrameV1::release) < 0.001f,
          "Silence generated a phantom peak or release");
  Require(CountEventTransitions(silence) <= 1,
          "Silence churned committed-state event IDs");

  const auto& lowGroove = replay[1].states;
  Require(RangeMean(lowGroove, 240, 360,
                    &MusicalStateFrameV1::grooveEnergy) >
              RangeMean(lowGroove, 0, 120,
                        &MusicalStateFrameV1::grooveEnergy) + 0.20f,
          "Steady low groove did not develop progressively");
  Require(RangeMin(lowGroove, 300, 360,
                   &MusicalStateFrameV1::grooveEnergy) > 0.35f,
          "Steady low groove did not persist between hits as a motion driver");
  Require(lowGroove.back().sustainedEnergy > silence.back().sustainedEnergy,
          "Steady low groove did not retain more energy than silence");
  Require(RangeMax(lowGroove, 0, lowGroove.size(),
                   &MusicalStateFrameV1::peak) < 0.05f &&
              CountEventTransitions(lowGroove) <= 1,
          "Steady low-groove hits behaved like unrelated peak events");

  const auto& transients = replay[2].states;
  Require(RangeMax(transients, 0, transients.size(),
                   &MusicalStateFrameV1::onsetEnvelope) > 0.50f,
          "Repeated transient groove had no strong onset envelope");
  Require(RangeMean(transients, 240, 360,
                    &MusicalStateFrameV1::grooveEnergy) > 0.30f,
          "Repeated transients did not establish groove memory");
  Require(transients[330].grooveEnergy > transients[30].grooveEnergy,
          "Repeated transient groove did not accumulate over time");
  Require(transients[300].onsetEnvelope >
              transients[329].onsetEnvelope + 0.45f,
          "Repeated transient onset did not decay between hits");
  Require(RangeMax(transients, 0, transients.size(),
                   &MusicalStateFrameV1::peak) < 0.25f &&
              CountEventTransitions(transients) <= 2,
          "Repeated transients continuously retriggered peak events");

  const auto& buildup = replay[3].states;
  Require(buildup.back().sustainedEnergy > buildup[120].sustainedEnergy + 0.25f,
          "Gradual buildup did not accumulate sustained energy");
  Require(RangeMax(buildup, 360, 600, &MusicalStateFrameV1::building) >
              RangeMax(buildup, 0, 180, &MusicalStateFrameV1::building) + 0.10f,
          "Gradual buildup did not strengthen building confidence");
  Require(RangeMean(buildup, 420, 540, &MusicalStateFrameV1::energyTrend) > 0.0f,
          "Gradual buildup lost its positive trend");
  Require(RangeMax(buildup, 0, buildup.size(),
                   &MusicalStateFrameV1::peak) < 0.15f,
          "Gradual buildup triggered a peak without a transition");

  const auto& drop = replay[4].states;
  Require(RangeMax(drop, 420, 540, &MusicalStateFrameV1::peak) >
              RangeMax(drop, 0, 300, &MusicalStateFrameV1::peak) + 0.10f,
          "Buildup-to-peak fixture did not distinguish the drop");
  Require(drop[450].immediateEnergy > drop[180].immediateEnergy,
          "Buildup-to-peak fixture lost impact contrast");
  const std::size_t buildingIndex = FirstIndexAbove(
      drop, 0, 420, &MusicalStateFrameV1::building, 0.50f);
  const std::size_t peakIndex = FirstIndexAbove(
      drop, 420, drop.size(), &MusicalStateFrameV1::peak, 0.50f);
  Require(buildingIndex < peakIndex && peakIndex < drop.size(),
          "Buildup did not precede the peak transition");
  Require(RangeMax(drop, 420, 560, &MusicalStateFrameV1::peak) >
              RangeMax(transients, 0, transients.size(),
                       &MusicalStateFrameV1::peak) + 0.35f,
          "Buildup-qualified peak was not stronger than ordinary transients");
  Require(drop[419].eventId != 0 &&
              HasDifferentEvent(drop, 420, 480, drop[419].eventId),
          "Buildup-to-peak transition did not mint a deterministic event ID");

  const auto& release = replay[5].states;
  const float releaseBefore = RangeMax(
      release, 0, 300, &MusicalStateFrameV1::release);
  const float releaseAfter = RangeMax(
      release, 360, 600, &MusicalStateFrameV1::release);
  if (!(releaseAfter > releaseBefore + 0.10f)) {
    std::ostringstream message;
    message << "Peak-to-release fixture did not create release confidence (before="
            << releaseBefore << ", after=" << releaseAfter << ')';
    throw std::runtime_error(message.str());
  }
  Require(RangeMax(release, 120, 300, &MusicalStateFrameV1::peak) >
              RangeMax(release, 480, 600,
                       &MusicalStateFrameV1::peak) + 0.40f,
          "Peak-to-release fixture did not let peak confidence fall");
  Require(release.back().release > release.back().peak + 0.10f &&
              release.back().release > 0.12f,
          "Release did not persist longer than the peak");
  Require(RangeMax(release, 210, 300,
                   &MusicalStateFrameV1::calm) < 0.11f,
          "Peak-to-release output snapped directly to calm");
  Require(release.back().sustainedEnergy < release[210].sustainedEnergy,
          "Peak-to-release fixture did not decay sustained energy");

  const auto& isolated = replay[6].states;
  Require(isolated[90].onsetEnvelope > isolated[89].onsetEnvelope + 0.50f,
          "Isolated transient did not create immediate impact");
  Require(isolated[150].onsetEnvelope < isolated[90].onsetEnvelope * 0.25f,
          "Isolated transient onset did not decay");
  Require(isolated.back().calm > isolated[90].calm,
          "Isolated transient did not return toward calm");
  Require(RangeMax(isolated, 0, isolated.size(),
                   &MusicalStateFrameV1::grooveEnergy) < 0.15f &&
              RangeMax(isolated, 0, isolated.size(),
                       &MusicalStateFrameV1::peak) < 0.15f &&
              CountEventTransitions(isolated) <= 1,
          "Isolated transient fabricated sustained groove or peak state");

  const auto& loud = replay[7].states;
  Require(loud.back().sustainedEnergy > 0.75f,
          "Sustained loud fixture did not retain sustained energy");
  Require(loud.back().onsetEnvelope < 0.05f,
          "Sustained loud fixture was treated as a fresh onset indefinitely");
  Require(RangeMean(loud, 360, 600, &MusicalStateFrameV1::driving) >
              RangeMean(silence, 0, 240, &MusicalStateFrameV1::driving) + 0.30f,
          "Sustained loud fixture did not become driving");
  Require(loud[30].onsetEnvelope > loud[120].onsetEnvelope + 0.80f,
          "Initial loud transition did not differ from the sustained state");
  Require(RangeMax(loud, 0, loud.size(),
                   &MusicalStateFrameV1::peak) < 0.25f &&
              CountEventTransitions(loud) <= 2,
          "Sustained loud input generated repeated false peak events");
}

SignalFrameV1 MakeSignal(std::uint64_t sampleIndex, float rms = 0.0f) {
  SignalFrameV1 signal{};
  signal.sampleIndex = sampleIndex;
  signal.rms = rms;
  signal.peak = std::min(1.0f, rms * 2.0f);
  signal.bass = rms;
  signal.mids = rms * 0.7f;
  signal.highs = rms * 0.5f;
  signal.bands.fill(rms);
  return signal;
}

void VerifySilenceDecay(const std::vector<SignalFrameV1>& silenceSignals) {
  MusicalStateEngine engine;
  const auto energetic = engine.Advance(MakeSignal(0, 0.80f));
  std::vector<MusicalStateFrameV1> decay;
  decay.reserve(silenceSignals.size());
  for (const auto& signal : silenceSignals) {
    decay.push_back(engine.Advance(signal));
  }
  Require(decay.front().sustainedEnergy < energetic.sustainedEnergy,
          "Silence did not begin decaying prior energy");
  Require(decay.back().sustainedEnergy < decay.front().sustainedEnergy * 0.30f,
          "Silence did not substantially decay prior energy");
  Require(decay.back().calm > decay.front().calm + 0.10f,
          "Calm did not recover while prior energy decayed");
}

void RequireCleanRebase(const MusicalStateFrameV1& frame,
                        std::string_view context) {
  Require(frame.eventId == 0 && frame.onsetEnvelope == 0.0f &&
              frame.accentEnvelope == 0.0f && frame.grooveEnergy == 0.0f &&
              frame.energyTrend == 0.0f && frame.calm == 0.0f &&
              frame.building == 0.0f && frame.driving == 0.0f &&
              frame.peak == 0.0f && frame.release == 0.0f &&
              frame.slowStateAge == 0.0f,
          context);
}

void VerifyMalformedAndClockEdges() {
  MusicalStateEngine malformed;
  (void)malformed.Advance(MakeSignal(800, 0.02f));
  SignalFrameV1 bad = MakeSignal(1600, 0.0f);
  bad.rms = std::numeric_limits<float>::quiet_NaN();
  bad.peak = std::numeric_limits<float>::infinity();
  bad.bass = -std::numeric_limits<float>::infinity();
  bad.mids = 50.0f;
  bad.highs = -4.0f;
  bad.bpm = std::numeric_limits<float>::quiet_NaN();
  bad.beatPhase = std::numeric_limits<float>::infinity();
  bad.beatConfidence = -10.0f;
  for (std::size_t index = 0; index < bad.bands.size(); ++index) {
    bad.bands[index] = index % 3 == 0
        ? std::numeric_limits<float>::quiet_NaN()
        : (index % 3 == 1 ? std::numeric_limits<float>::infinity() : -8.0f);
  }
  RequireFiniteAndBounded(malformed.Advance(bad));

  SignalFrameV1 clipped = MakeSignal(2400, 8.0f);
  clipped.peak = 100.0f;
  clipped.bass = 20.0f;
  clipped.mids = -20.0f;
  clipped.highs = 5.0f;
  clipped.bands.fill(99.0f);
  RequireFiniteAndBounded(malformed.Advance(clipped));

  MusicalStateEngine duplicate;
  (void)duplicate.Advance(MakeSignal(800, 0.01f));
  SignalFrameV1 onset = MakeSignal(1600, 0.3f);
  onset.hit = true;
  onset.accent = true;
  onset.hitCount = 1;
  onset.accentCount = 1;
  const auto beforeDuplicate = duplicate.Advance(onset);
  SignalFrameV1 changedDuplicate = MakeSignal(1600, 1.0f);
  changedDuplicate.hit = true;
  changedDuplicate.accent = true;
  changedDuplicate.hitCount = 999;
  changedDuplicate.accentCount = 999;
  const auto afterDuplicate = duplicate.Advance(changedDuplicate);
  Require(ExactFieldsEqual(beforeDuplicate, afterDuplicate),
          "Duplicate sample index changed state");

  // Duplicate-index identity has precedence over payload consistency. Even a
  // counter rewind on the same analyzer publication is ignored exactly; once
  // the source clock advances, that rewind becomes an explicit reconnect.
  SignalFrameV1 rewoundDuplicate = MakeSignal(1600, 0.0f);
  rewoundDuplicate.hitCount = 0;
  rewoundDuplicate.accentCount = 0;
  const auto afterRewoundDuplicate = duplicate.Advance(rewoundDuplicate);
  Require(ExactFieldsEqual(beforeDuplicate, afterRewoundDuplicate),
          "Counter rewind overrode duplicate-index exactness");

  SignalFrameV1 jump = MakeSignal(48000ULL * 3600ULL, 1.0f);
  jump.hitCount = 0;
  jump.accentCount = 0;
  const auto afterJump = duplicate.Advance(jump);
  RequireFiniteAndBounded(afterJump);
  Require(afterJump.sourceSampleIndex == jump.sampleIndex,
          "Large sample jump lost source identity");
  RequireCleanRebase(afterJump,
                     "Advanced counter rewind did not cleanly rebase state");

  MusicalStateEngine cappedGap;
  MusicalStateEngine largeGap;
  (void)cappedGap.Advance(MakeSignal(800, 0.4f));
  (void)largeGap.Advance(MakeSignal(800, 0.4f));
  const auto cappedStep = cappedGap.Advance(MakeSignal(12'800, 0.0f));
  const auto largeStep = largeGap.Advance(
      MakeSignal(48'000ULL * 3'600ULL, 0.0f));
  Require(ExactFieldsExceptSourceEqual(cappedStep, largeStep),
          "Large sample gap did not match the configured bounded time step");

  MusicalStateEngine counters;
  SignalFrameV1 countAnchor = MakeSignal(800, 0.04f);
  countAnchor.hitCount = 10;
  countAnchor.accentCount = 5;
  (void)counters.Advance(countAnchor);
  SignalFrameV1 countEdge = MakeSignal(1600, 0.3f);
  countEdge.hit = true;
  countEdge.accent = true;
  countEdge.hitCount = 11;
  countEdge.accentCount = 6;
  const auto counted = counters.Advance(countEdge);
  Require(counted.onsetEnvelope > 0.5f,
          "Counter edge did not create an onset");
  Require(counted.eventId == 0,
          "Raw counter edge bypassed slow-state dwell before event minting");
  SignalFrameV1 countReset = MakeSignal(2400, 0.3f);
  countReset.hit = true;
  countReset.accent = true;
  countReset.hitCount = 0;
  countReset.accentCount = 0;
  const auto afterCounterReset = counters.Advance(countReset);
  RequireCleanRebase(afterCounterReset,
                     "Counter reset replayed phantom musical state");

  SignalFrameV1 afterReset = MakeSignal(3200, 0.3f);
  afterReset.hit = true;
  afterReset.accent = true;
  afterReset.hitCount = 0;
  afterReset.accentCount = 0;
  const auto recovered = counters.Advance(afterReset);
  RequireFiniteAndBounded(recovered);
  Require(recovered.onsetEnvelope == 0.0f &&
              recovered.accentEnvelope == 0.0f && recovered.peak < 0.05f,
          "Held reconnect flags generated a phantom event after counter reset");

  MusicalStateEngine rewind;
  (void)rewind.Advance(MakeSignal(800, 0.01f));
  (void)rewind.Advance(MakeSignal(1600, 0.5f));
  SignalFrameV1 rewindFrame = MakeSignal(400, 0.5f);
  rewindFrame.hit = true;
  rewindFrame.accent = true;
  rewindFrame.hitCount = 99;
  rewindFrame.accentCount = 99;
  const auto rewound = rewind.Advance(rewindFrame);
  Require(rewound.sourceSampleIndex == 400,
          "Sample rewind lost its new source anchor");
  RequireCleanRebase(rewound,
                     "Sample rewind replayed phantom reconnect state");
  rewindFrame.sampleIndex = 1200;
  const auto rewindRecovered = rewind.Advance(rewindFrame);
  RequireFiniteAndBounded(rewindRecovered);
  Require(rewindRecovered.onsetEnvelope == 0.0f &&
              rewindRecovered.accentEnvelope == 0.0f &&
              rewindRecovered.peak < 0.05f,
          "Held reconnect flags generated a phantom event after sample rewind");

  MusicalStateEngine held;
  (void)held.Advance(MakeSignal(800, 0.0f));
  SignalFrameV1 heldHit = MakeSignal(1600, 0.25f);
  heldHit.hit = true;
  heldHit.hitCount = 1;
  const auto firstHit = held.Advance(heldHit);
  heldHit.sampleIndex = 2400;
  const auto secondHeld = held.Advance(heldHit);
  Require(secondHeld.onsetEnvelope < firstHit.onsetEnvelope,
          "Held hit retriggered onset");
  Require(secondHeld.eventId == firstHit.eventId,
          "Held hit minted a second event ID");

  MusicalStateEngine sampleRate;
  (void)sampleRate.Advance(MakeSignal(800, 0.0f));
  SignalFrameV1 rateHit = MakeSignal(1600, 0.5f);
  rateHit.hit = true;
  rateHit.hitCount = 1;
  Require(sampleRate.Advance(rateHit).onsetEnvelope > 0.5f,
          "Sample-rate reset setup did not establish temporal state");
  sampleRate.SetSampleRate(96000);
  Require(sampleRate.SampleRate() == 96000,
          "Real sample-rate change was not applied");
  Require(ExactFieldsEqual(sampleRate.Current(), MusicalStateFrameV1{}),
          "Real sample-rate change did not clear temporal state");

  sampleRate.SetSampleRate(0);
  Require(sampleRate.SampleRate() == 48000,
          "Zero sample rate did not sanitize to 48 kHz");

  MusicalStateEngineConfig lowRateConfig{};
  lowRateConfig.sampleRate = 24000;
  MusicalStateEngineConfig highRateConfig{};
  highRateConfig.sampleRate = 96000;
  MusicalStateEngine lowRate(lowRateConfig);
  MusicalStateEngine highRate(highRateConfig);
  (void)lowRate.Advance(MakeSignal(0, 0.0f));
  (void)highRate.Advance(MakeSignal(0, 0.0f));
  const auto lowRateStep = lowRate.Advance(MakeSignal(2400, 0.8f));
  const auto highRateStep = highRate.Advance(MakeSignal(2400, 0.8f));
  Require(lowRateStep.sustainedEnergy > highRateStep.sustainedEnergy,
          "Sample rate did not scale sample-clock elapsed time");
}

void VerifyRenderCadenceIndependence(
    const std::vector<SignalFrameV1>& signals) {
  MusicalStateEngineConfig config{};
  config.seed = kReplaySeed;
  MusicalStateEngine sourceCadence(config);
  MusicalStateEngine fasterRenderCadence(config);
  for (const auto& signal : signals) {
    const auto sourceFrame = sourceCadence.Advance(signal);
    const auto renderFrame = fasterRenderCadence.Advance(signal);
    Require(ExactFieldsEqual(sourceFrame, renderFrame),
            "Render cadence changed the advancing musical-state sequence");

    SignalFrameV1 duplicate = signal;
    duplicate.rms = signal.rms > 0.5f ? 0.0f : 1.0f;
    duplicate.peak = signal.peak > 0.5f ? 0.0f : 1.0f;
    duplicate.bands.fill(1.0f);
    duplicate.hit = !signal.hit;
    duplicate.accent = !signal.accent;
    duplicate.hitCount += 10'000;
    duplicate.accentCount += 10'000;
    const auto extraRenderFrame = fasterRenderCadence.Advance(duplicate);
    Require(ExactFieldsEqual(renderFrame, extraRenderFrame),
            "Extra same-timestamp render publication changed musical state");
  }
}

void VerifySeedIsolation(const std::vector<SignalFrameV1>& signals) {
  MusicalStateEngineConfig firstConfig{};
  firstConfig.seed = 0x1111111111111111ULL;
  MusicalStateEngineConfig secondConfig = firstConfig;
  secondConfig.seed = 0x2222222222222222ULL;
  MusicalStateEngine first(firstConfig);
  MusicalStateEngine second(secondConfig);
  bool observedDifferentEvent = false;
  for (const auto& signal : signals) {
    const auto firstFrame = first.Advance(signal);
    const auto secondFrame = second.Advance(signal);
    Require(ExactFieldsExceptEventEqual(firstFrame, secondFrame),
            "Seed changed musical state instead of only event IDs");
    if (firstFrame.eventId != secondFrame.eventId) {
      observedDifferentEvent = true;
    }
  }
  Require(observedDifferentEvent, "Alternate seeds never changed an event ID");
}

void VerifyBoundedLongRun() {
  MusicalStateEngineConfig config{};
  config.seed = 0xabcdef0123456789ULL;
  MusicalStateEngine engine(config);
  std::uint64_t hitCount = 0;
  std::uint64_t accentCount = 0;
  for (std::uint64_t index = 0; index < 120000; ++index) {
    const std::uint64_t sampleIndex = (index + 1) * 800;
    const float cycle = static_cast<float>(index % 3600) / 3599.0f;
    float energy = cycle < 0.5f ? cycle * 0.8f : (1.0f - cycle) * 0.8f;
    energy += 0.015f;
    SignalFrameV1 signal = MakeSignal(sampleIndex, energy);
    signal.hit = index % 30 == 0;
    signal.accent = index % 120 == 0;
    if (signal.hit) ++hitCount;
    if (signal.accent) ++accentCount;
    signal.hitCount = hitCount;
    signal.accentCount = accentCount;
    signal.bpm = 120.0f;
    signal.beatPhase = static_cast<float>(index % 30) / 30.0f;
    signal.beatConfidence = 0.95f;
    const auto frame = engine.Advance(signal);
    if ((index & 0xffU) == 0U) RequireFiniteAndBounded(frame);
  }
  RequireFiniteAndBounded(engine.Current());
  Require(engine.Current().slowStateAge <= 1.0f,
          "Long-run slow-state age did not saturate");
}

}  // namespace

int main() {
  try {
    // Replace only when the explicit state contract or intentional algorithm
    // behavior changes, after reviewing semantic assertions and replay diffs.
    constexpr std::array specs{
        FixtureSpec{"silence / near silence", "a_silence.csv", 240,
                    0x1f4c49e49210ef9bULL},
        FixtureSpec{"steady low groove", "b_steady_low_groove.csv", 360,
                    0x6635b2213d83e427ULL},
        FixtureSpec{"repeated transient groove", "c_repeated_transient_groove.csv", 360,
                    0x8602c2dee9ddc7baULL},
        FixtureSpec{"gradual buildup", "d_gradual_buildup.csv", 600,
                    0xae2b3cd1dfe807eaULL},
        FixtureSpec{"buildup to peak", "e_buildup_to_peak.csv", 600,
                    0x86242b387ed87ea2ULL},
        FixtureSpec{"peak to release", "f_peak_to_release.csv", 600,
                    0xe536f2572e29b28aULL},
        FixtureSpec{"isolated transient", "g_isolated_transient.csv", 240,
                    0x93f657eec4beaebdULL},
        FixtureSpec{"sustained loud", "h_sustained_loud.csv", 600,
                    0x1f29e40b49eb5c63ULL},
    };

    std::array<FixtureReplay, specs.size()> replay{};
    for (std::size_t index = 0; index < specs.size(); ++index) {
      replay[index] = ReplayFixture(specs[index]);
    }
    VerifyFixtureSemantics(replay);
    VerifySilenceDecay(replay[0].signals);
    VerifyMalformedAndClockEdges();
    VerifyRenderCadenceIndependence(replay[4].signals);
    VerifySeedIsolation(replay[4].signals);
    VerifyBoundedLongRun();
    std::cout << "PrismForge musical-state replay tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "PrismForge musical-state replay tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
