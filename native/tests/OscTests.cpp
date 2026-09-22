#include "../engine/OscBridge.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

using namespace prismforge;

#define CHECK(condition) do {                                              \
  if (!(condition)) {                                                       \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition     \
              << '\n';                                                     \
    return 1;                                                               \
  }                                                                         \
} while (false)

namespace {
using Bytes = std::vector<std::byte>;

void String(Bytes& bytes, std::string_view value) {
  for (const char ch : value) bytes.push_back(std::byte(static_cast<unsigned char>(ch)));
  bytes.push_back(std::byte{0});
  while (bytes.size() % 4) bytes.push_back(std::byte{0});
}

void Word(Bytes& bytes, std::uint32_t value) {
  bytes.push_back(std::byte(value >> 24));
  bytes.push_back(std::byte(value >> 16));
  bytes.push_back(std::byte(value >> 8));
  bytes.push_back(std::byte(value));
}

Bytes FloatCommand(std::string_view path, float value) {
  Bytes bytes;
  String(bytes, path);
  String(bytes, ",f");
  Word(bytes, std::bit_cast<std::uint32_t>(value));
  return bytes;
}

Bytes Gesture() {
  Bytes bytes;
  String(bytes, "/prism/gesture");
  String(bytes, ",sfff");
  String(bytes, "swipe_right");
  Word(bytes, std::bit_cast<std::uint32_t>(0.7f));
  Word(bytes, std::bit_cast<std::uint32_t>(0.25f));
  Word(bytes, std::bit_cast<std::uint32_t>(0.75f));
  return bytes;
}

Bytes Bundle(const Bytes& first, const Bytes& second) {
  Bytes bytes;
  String(bytes, "#bundle");
  Word(bytes, 0);
  Word(bytes, 1);
  Word(bytes, static_cast<std::uint32_t>(first.size()));
  bytes.insert(bytes.end(), first.begin(), first.end());
  Word(bytes, static_cast<std::uint32_t>(second.size()));
  bytes.insert(bytes.end(), second.begin(), second.end());
  return bytes;
}
}  // namespace

int main() {
  DecodedOscPacket packet;
  auto crossfader = FloatCommand("/prismforge/crossfader", 0.625f);
  CHECK(DecodeOscDatagram(crossfader, OscBridge::kCommandPort, packet));
  CHECK(packet.commands.size() == 1 && packet.gestures.empty());
  CHECK(packet.commands[0].name == "setCrossfader");
  CHECK(packet.commands[0].payload.at("value").get<float>() == 0.625f);

  Bytes scene;
  String(scene, "/prismforge/deck/B/scene");
  String(scene, ",s");
  String(scene, "neon-rift");
  CHECK(DecodeOscDatagram(scene, OscBridge::kCommandPort, packet));
  CHECK(packet.commands.size() == 1);
  CHECK(packet.commands[0].payload.at("deck") == "B");

  Bytes blackout;
  String(blackout, "/prismforge/blackout");
  String(blackout, ",F");
  CHECK(DecodeOscDatagram(blackout, OscBridge::kCommandPort, packet));
  CHECK(packet.commands[0].name == "setBlackout");
  CHECK(packet.commands[0].payload.at("enabled") == false);

  const auto gesture = Gesture();
  CHECK(DecodeOscDatagram(gesture, OscBridge::kGesturePort, packet));
  CHECK(packet.commands.empty() && packet.gestures.size() == 1);
  CHECK(packet.gestures[0].name == "swipe_right");
  CHECK(packet.gestures[0].strength == 0.7f);
  CHECK(packet.gestures[0].centerX == 0.25f);
  CHECK(packet.gestures[0].centerY == 0.75f);
  CHECK(DecodeOscDatagram(gesture, OscBridge::kCommandPort, packet));
  CHECK(packet.commands.empty() && packet.gestures.empty());

  const auto bundled = Bundle(crossfader, blackout);
  CHECK(DecodeOscDatagram(bundled, OscBridge::kCommandPort, packet));
  CHECK(packet.commands.size() == 2);
  CHECK(packet.commands[0].name == "setCrossfader");
  CHECK(packet.commands[1].name == "setBlackout");

  auto truncated = bundled;
  truncated.pop_back();
  CHECK(!DecodeOscDatagram(truncated, OscBridge::kCommandPort, packet));
  CHECK(packet.commands.size() == 2);  // malformed bundle is atomic

  auto nan = FloatCommand("/prismforge/crossfader",
                          std::bit_cast<float>(0x7fc00000u));
  CHECK(!DecodeOscDatagram(nan, OscBridge::kCommandPort, packet));
  CHECK(packet.commands.size() == 2);

  auto over = FloatCommand("/prismforge/crossfader", 1.01f);
  CHECK(!DecodeOscDatagram(over, OscBridge::kCommandPort, packet));
  CHECK(packet.commands.size() == 2);

  auto badPadding = crossfader;
  badPadding[23] = std::byte{1};
  CHECK(!DecodeOscDatagram(badPadding, OscBridge::kCommandPort, packet));
  CHECK(packet.commands.size() == 2);

  std::cout << "OSC parser tests passed\n";
}
