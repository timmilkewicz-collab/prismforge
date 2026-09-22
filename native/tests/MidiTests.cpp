#include "MidiBridge.h"

#include <bitset>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#define CHECK(condition) do { \
  if (!(condition)) throw std::runtime_error("Check failed: " #condition); \
} while (false)

using namespace prismforge;

namespace {
constexpr std::uint8_t kDark = 12;

MidiMessage Down(std::uint8_t note, std::uint32_t timeMs) {
  return {0x90, note, 127, timeMs};
}

MidiMessage Up(std::uint8_t note, std::uint32_t timeMs) {
  return {0x80, note, 0, timeMs};
}
}  // namespace

int main() {
  CHECK(ValidateLaunchpadSProfile());
  ShowState show;
  std::array<bool, 8> cues{};
  LaunchpadSInterpreter interpreter;
  auto leds = BuildLaunchpadSLeds(show.Current(), cues, interpreter.Pressed());
  unsigned darkCount = 0;
  for (const auto velocity : leds) {
    if (velocity == kDark) ++darkCount;
  }
  // Thirty physical controls are intentionally unmapped, plus eight empty
  // recall pads that are mapped but unavailable until saved.
  CHECK(darkCount == 38);
  std::bitset<80> mapped;
  for (const auto& binding : LaunchpadSBindings()) {
    const unsigned slot = binding.command == 0xB0
        ? 72u + binding.number - 104u
        : (binding.number / 16u) * 9u + binding.number % 16u;
    CHECK(slot < mapped.size());
    CHECK(!mapped.test(slot));
    mapped.set(slot);
  }
  CHECK(mapped.count() == 50);
  for (std::size_t slot = 0; slot < leds.size(); ++slot) {
    if (!mapped.test(slot)) CHECK(leds[slot] == kDark);
  }
  CHECK(leds[0] == 60);  // Deck A Ink Tide is selected.
  CHECK(leds[18] == 28); // Deck B Ink Tide is not selected.
  CHECK(leds[19] == 60); // Deck B Prism Atrium is selected.
  CHECK(leds[1] == 28);  // Other deck scene is dim green.
  CHECK(leds[72] == 47); // First crossfader step is selected, orange.
  CHECK(leds[73] == 30); // Other step is dim orange.
  CHECK(leds[45] == 13); // Blackout safety key is dim red.
  CHECK(leds[46] == 29); // Panic-dim toggle is dim amber.

  auto action = interpreter.Process(Down(1, 10));
  CHECK(action && action->type == LaunchpadActionType::SetScene);
  CHECK(action->deck == 0 && action->index == 1);
  CHECK(!interpreter.Process(Down(1, 11))); // Held repeat suppressed.
  CHECK(!interpreter.Process(Up(1, 12)));
  CHECK(show.SetScene(action->deck, kSceneIds[action->index]));
  leds = BuildLaunchpadSLeds(show.Current(), cues, interpreter.Pressed());
  CHECK(leds[0] == 28 && leds[1] == 60);

  action = interpreter.Process({0xB0, 111, 127, 20});
  CHECK(action && action->type == LaunchpadActionType::SetCrossfader);
  CHECK(std::abs(action->value - 1.0f) < 0.001f);
  CHECK(!interpreter.Process({0xB0, 111, 0, 21}));
  show.SetCrossfader(action->value);
  leds = BuildLaunchpadSLeds(show.Current(), cues, interpreter.Pressed());
  CHECK(leds[72] == 30 && leds[79] == 47);

  // Right-edge save cue is guarded: a short tap does not overwrite a cue.
  CHECK(!interpreter.Process(Down(8, 100)));
  CHECK(!interpreter.Process(Up(8, 1299)));
  CHECK(!interpreter.Process(Down(8, 2000)));
  action = interpreter.Process(Up(8, 3200));
  CHECK(action && action->type == LaunchpadActionType::SaveCue);
  CHECK(action->index == 0);
  CHECK(!interpreter.Process(Up(8, 3201))); // Cannot release twice.

  // Cue recalls are mapped but dark until there is a saved cue.
  CHECK(leds[36] == kDark);
  cues[0] = true;
  leds = BuildLaunchpadSLeds(show.Current(), cues, interpreter.Pressed());
  CHECK(leds[36] == 45);
  action = interpreter.Process(Down(64, 3300));
  CHECK(action && action->type == LaunchpadActionType::RecallCue);
  CHECK(action->index == 0);
  leds = BuildLaunchpadSLeds(show.Current(), cues, interpreter.Pressed());
  CHECK(leds[36] == 62);
  CHECK(!interpreter.Process(Up(64, 3310)));

  action = interpreter.Process(Down(80, 3400));
  CHECK(action && action->type == LaunchpadActionType::ToggleBlackout);
  show.SetBlackout(true);
  leds = BuildLaunchpadSLeds(show.Current(), cues, interpreter.Pressed());
  CHECK(leds[45] == 15);
  CHECK(!interpreter.Process(Up(80, 3410)));
  action = interpreter.Process(Down(81, 3500));
  CHECK(action && action->type == LaunchpadActionType::TogglePanicDim);
  show.SetPanicDim(true);
  leds = BuildLaunchpadSLeds(show.Current(), cues, interpreter.Pressed());
  CHECK(leds[46] == 63);

  CHECK(!interpreter.Process(Down(127, 3600))); // Not physical/mapped.
  CHECK(!interpreter.Process({0x91, 0, 127, 3700})); // Other channel ignored.
  interpreter.Reset();
  CHECK(interpreter.Pressed().none());

  BoundedQueue<MidiMessage> queue(4);
  MidiBridge bridge(queue);
  CHECK(!bridge.IsOpen()); // Merely constructing never takes the live port.
  CHECK(!bridge.FeedbackEnabled());

  std::cout << "PrismForge MIDI profile tests passed (50 unique bindings, mapped-only LEDs).\n";
  return 0;
}
