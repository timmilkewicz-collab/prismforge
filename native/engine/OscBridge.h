#pragma once

#include "PipeServer.h"

#include "PrismForge/BoundedQueue.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace prismforge {

// The Kinect event is transport data until the engine/render thread consumes it.
// PrismBurst sends /prism/gesture <name:string> <strength:float>
// <centerX01:float> <centerY01:float> to 127.0.0.1:12002.
struct GestureEvent {
  std::string name;
  float strength = 0.0f;
  float centerX = 0.5f;
  float centerY = 0.5f;
  std::chrono::steady_clock::time_point receivedAt{};
};

struct DecodedOscPacket {
  std::vector<QueuedCommand> commands;
  std::vector<GestureEvent> gestures;
};

// Pure decoder for tests and offline fixtures. Never mutates the output on a
// malformed packet. Unknown valid addresses are ignored. Only localCommandPort
// 12100 accepts PrismForge commands; only gesturePort 12002 accepts gestures.
bool DecodeOscDatagram(std::span<const std::byte> packet,
                       std::uint16_t destinationPort,
                       DecodedOscPacket& output);

class OscBridge {
 public:
  static constexpr std::uint16_t kCommandPort = 12100;
  static constexpr std::uint16_t kGesturePort = 12002;
  static constexpr std::uint16_t kStatusPort = 12101;

  OscBridge(BoundedQueue<QueuedCommand>& commands,
            BoundedQueue<GestureEvent>& gestures);
  ~OscBridge();
  OscBridge(const OscBridge&) = delete;
  OscBridge& operator=(const OscBridge&) = delete;

  // Both listeners bind 127.0.0.1 only. A port conflict fails Start instead of
  // silently accepting remote traffic or stealing another listener's packets.
  bool Start(std::string& error);
  void Stop();

  // Optional best-effort, nonblocking OSC status to 127.0.0.1:12101:
  // /prismforge/status ,fifi <fps> <qualityTier> <rms> <spoutReady:0|1>
  bool SendStatus(float fps, std::int32_t qualityTier, float rms,
                  bool spoutReady) noexcept;

  // Read-only, size-bounded, sanitized telemetry from the exact AppData file.
  // 'available' is file/JSON validity; 'fresh' is a recent timestamp, not proof
  // that the live PrismBurst output is actually delivering pixels.
  static nlohmann::json ReadPrismBurstHealth();

  [[nodiscard]] std::uint64_t DroppedCommands() const noexcept;
  [[nodiscard]] std::uint64_t DroppedGestures() const noexcept;
  [[nodiscard]] std::uint64_t RejectedPackets() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace prismforge
