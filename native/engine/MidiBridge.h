#pragma once

#include "PrismForge/BoundedQueue.h"
#include "PrismForge/ShowState.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace prismforge {

// WinMM's callback copies only these four bytes into a bounded queue. The
// engine thread owns interpretation and all ShowState mutations.
struct MidiMessage {
  std::uint8_t status = 0;
  std::uint8_t data1 = 0;
  std::uint8_t data2 = 0;
  std::uint32_t timestampMs = 0;
};

struct MidiDeviceInfo {
  unsigned id = 0;
  std::string name;
};

enum class LaunchpadActionType {
  SetScene,
  RecallCue,
  SaveCue,
  SetCrossfader,
  ToggleBlackout,
  TogglePanicDim,
};

struct LaunchpadAction {
  LaunchpadActionType type{};
  unsigned deck = 0;
  unsigned index = 0;
  float value = 0.0f;
};

struct LaunchpadBinding {
  std::uint8_t command = 0;  // 0x90 note or 0xB0 control change, channel 0.
  std::uint8_t number = 0;
  LaunchpadAction action{};
  bool guarded = false;
};

// Slots 0..71: eight grid rows of nine note controls, including the right
// scene button. Slots 72..79: top CC 104..111. Unmapped slots are always
// velocity 12, the documented dark/off value for Launchpad S.
using LaunchpadLedFrame = std::array<std::uint8_t, 80>;

[[nodiscard]] const std::array<LaunchpadBinding, 50>& LaunchpadSBindings() noexcept;
[[nodiscard]] bool ValidateLaunchpadSProfile() noexcept;
[[nodiscard]] LaunchpadLedFrame BuildLaunchpadSLeds(
    const ShowSnapshot& show, const std::array<bool, 8>& cueSaved,
    const std::bitset<80>& pressed) noexcept;

// A one-way ownership decision: once PrismBurst appears, this PrismForge
// session must release the controller rather than silently reclaiming it.
class LaunchpadOwnershipGate {
 public:
  [[nodiscard]] bool Observe(bool prismBurstActive) noexcept {
    yielded_ = yielded_ || prismBurstActive;
    return yielded_;
  }
  [[nodiscard]] bool Yielded() const noexcept { return yielded_; }

 private:
  bool yielded_ = false;
};

// Pure, deterministic engine-thread state machine. Guarded save requires a
// complete press/release lasting at least 1200 ms. Repeated Note On messages
// while held cannot trigger the same action twice.
class LaunchpadSInterpreter {
 public:
  [[nodiscard]] std::optional<LaunchpadAction> Process(const MidiMessage& message) noexcept;
  [[nodiscard]] const std::bitset<80>& Pressed() const noexcept { return pressed_; }
  void Reset() noexcept;

 private:
  std::bitset<80> pressed_{};
  std::array<std::uint32_t, 80> pressedAtMs_{};
};

class MidiBridge {
 public:
  explicit MidiBridge(BoundedQueue<MidiMessage>& input);
  ~MidiBridge();
  MidiBridge(const MidiBridge&) = delete;
  MidiBridge& operator=(const MidiBridge&) = delete;

  [[nodiscard]] static std::vector<MidiDeviceInfo> InputDevices();
  [[nodiscard]] static std::vector<MidiDeviceInfo> OutputDevices();

  // Neither constructor nor enumeration opens a port. The caller must opt in
  // to one of these operations. Generic input produces raw messages only.
  bool OpenInput(unsigned deviceId, std::string& error);
  // Selects the first exact-name Launchpad S pair. Refuses while PrismBurst
  // appears active or WinMM reports a busy port. Feedback is opt-in as well.
  bool OpenLaunchpadS(bool feedback, std::string& error);
  void Stop() noexcept;

  // Enqueues one complete desired LED state; the output worker diffs frames
  // and sends WinMM messages. Never call from a MIDI callback.
  bool QueueFeedback(const LaunchpadLedFrame& frame) noexcept;
  [[nodiscard]] bool IsOpen() const noexcept;
  [[nodiscard]] bool FeedbackEnabled() const noexcept;
  [[nodiscard]] bool YieldedToPrismBurst() const noexcept;
  [[nodiscard]] std::uint64_t DroppedInput() const noexcept;
  [[nodiscard]] std::uint64_t DroppedFeedback() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace prismforge
