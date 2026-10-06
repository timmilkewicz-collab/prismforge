#pragma once

#include "PrismForge/MusicalStateEngine.h"
#include "PrismForge/SignalAnalyzer.h"

#include <string>

#include <nlohmann/json.hpp>

namespace prismforge {

// Builds the developer-facing SignalFrame payload sent over the existing local
// pipe. Musical interpretation is nested and additive so older v1 clients may
// continue to ignore fields they do not understand.
[[nodiscard]] nlohmann::json BuildSignalTelemetry(
    const SignalFrameV1& signal,
    const MusicalStateFrameV1& musical,
    unsigned sampleRate,
    double fps,
    double frameMs,
    unsigned targetFps,
    unsigned tier,
    float gestureMotion,
    const std::string& gestureName);

}  // namespace prismforge
