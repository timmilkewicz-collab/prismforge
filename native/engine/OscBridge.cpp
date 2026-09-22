#include "OscBridge.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace prismforge {
namespace {
constexpr std::size_t kMaxPacketBytes = 4096;
constexpr std::size_t kMaxStringBytes = 256;
constexpr std::size_t kMaxArguments = 8;
constexpr std::size_t kMaxMessagesPerPacket = 16;
using OscValue = std::variant<std::int32_t, float, std::string, bool>;

struct OscMessage {
  std::string address;
  std::vector<OscValue> arguments;
};

std::uint32_t ReadBigEndian32(const std::byte* data) noexcept {
  return (std::to_integer<std::uint32_t>(data[0]) << 24) |
         (std::to_integer<std::uint32_t>(data[1]) << 16) |
         (std::to_integer<std::uint32_t>(data[2]) << 8) |
         std::to_integer<std::uint32_t>(data[3]);
}

bool ReadOscString(std::span<const std::byte> data, std::size_t& offset,
                   std::string& result) {
  if (offset >= data.size()) return false;
  const auto start = offset;
  while (offset < data.size() && data[offset] != std::byte{0}) {
    if (offset - start >= kMaxStringBytes) return false;
    ++offset;
  }
  if (offset == data.size()) return false;
  const auto stringEnd = offset;
  const auto aligned = (offset + 4u) & ~std::size_t{3};
  if (aligned > data.size()) return false;
  for (auto index = offset; index < aligned; ++index) {
    if (data[index] != std::byte{0}) return false;
  }
  result.assign(reinterpret_cast<const char*>(data.data() + start), stringEnd - start);
  offset = aligned;
  return true;
}

bool ParseMessage(std::span<const std::byte> data, OscMessage& message) {
  std::size_t offset = 0;
  if (!ReadOscString(data, offset, message.address) ||
      message.address.empty() || message.address.front() != '/') return false;
  std::string tags;
  if (!ReadOscString(data, offset, tags) || tags.empty() || tags.front() != ',' ||
      tags.size() - 1 > kMaxArguments) return false;
  message.arguments.reserve(tags.size() - 1);
  for (std::size_t index = 1; index < tags.size(); ++index) {
    const char tag = tags[index];
    if (tag == 'T' || tag == 'F') {
      message.arguments.emplace_back(tag == 'T');
      continue;
    }
    if (tag == 's') {
      std::string value;
      if (!ReadOscString(data, offset, value)) return false;
      message.arguments.emplace_back(std::move(value));
      continue;
    }
    if (tag != 'i' && tag != 'f') return false;
    if (data.size() - offset < 4) return false;
    const std::uint32_t bits = ReadBigEndian32(data.data() + offset);
    offset += 4;
    if (tag == 'i') {
      message.arguments.emplace_back(std::bit_cast<std::int32_t>(bits));
    } else {
      const float value = std::bit_cast<float>(bits);
      if (!std::isfinite(value)) return false;
      message.arguments.emplace_back(value);
    }
  }
  return offset == data.size();
}

bool ParseMessages(std::span<const std::byte> data, unsigned depth,
                   std::vector<OscMessage>& messages) {
  if (data.empty() || data.size() > kMaxPacketBytes || depth > 2) return false;
  constexpr std::array<std::byte, 8> bundlePrefix{
      std::byte{'#'}, std::byte{'b'}, std::byte{'u'}, std::byte{'n'},
      std::byte{'d'}, std::byte{'l'}, std::byte{'e'}, std::byte{0}};
  if (data.size() >= bundlePrefix.size() &&
      std::equal(bundlePrefix.begin(), bundlePrefix.end(), data.begin())) {
    if (data.size() < 16) return false;
    // Timed bundles require a scheduler; only OSC's immediate timetag is safe.
    for (std::size_t i = 8; i < 15; ++i) {
      if (data[i] != std::byte{0}) return false;
    }
    if (data[15] != std::byte{1}) return false;
    std::size_t offset = 16;
    while (offset < data.size()) {
      if (messages.size() >= kMaxMessagesPerPacket || data.size() - offset < 4) return false;
      const auto length = ReadBigEndian32(data.data() + offset);
      offset += 4;
      if (length == 0 || length > data.size() - offset || length % 4 != 0) return false;
      if (!ParseMessages(data.subspan(offset, length), depth + 1, messages)) return false;
      offset += length;
    }
    return offset == data.size();
  }
  if (messages.size() >= kMaxMessagesPerPacket) return false;
  OscMessage message;
  if (!ParseMessage(data, message)) return false;
  messages.emplace_back(std::move(message));
  return true;
}

template <typename T>
std::optional<T> Argument(const OscMessage& message, std::size_t index) {
  if (index >= message.arguments.size()) return std::nullopt;
  if (const auto* value = std::get_if<T>(&message.arguments[index])) return *value;
  return std::nullopt;
}

bool UnitFloat(const OscMessage& message, std::size_t index, float& result) {
  const auto value = Argument<float>(message, index);
  if (!value || *value < 0.0f || *value > 1.0f) return false;
  result = *value;
  return true;
}

std::optional<bool> OscBoolean(const OscMessage& message, std::size_t index) {
  if (auto value = Argument<bool>(message, index)) return value;
  if (auto value = Argument<std::int32_t>(message, index)) {
    if (*value == 0 || *value == 1) return *value == 1;
  }
  return std::nullopt;
}

bool DecodeRecognized(const OscMessage& message, std::uint16_t port,
                      DecodedOscPacket& output) {
  const auto& path = message.address;
  const auto& args = message.arguments;
  if (port == OscBridge::kGesturePort) {
    if (path != "/prism/gesture") return true;
    if (args.size() != 4) return false;
    const auto name = Argument<std::string>(message, 0);
    float strength = 0.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    if (!name || (*name != "punch" && *name != "swipe_left" &&
                  *name != "swipe_right" && *name != "lift" &&
                  *name != "drop") ||
        !UnitFloat(message, 1, strength) ||
        !UnitFloat(message, 2, centerX) ||
        !UnitFloat(message, 3, centerY)) return false;
    output.gestures.push_back({*name, strength, centerX, centerY,
                               std::chrono::steady_clock::now()});
    return true;
  }
  if (port != OscBridge::kCommandPort) return false;
  QueuedCommand command;
  command.requestId = "osc";
  if (path == "/prismforge/crossfader") {
    float value = 0.0f;
    if (args.size() != 1 || !UnitFloat(message, 0, value)) return false;
    command = {"setCrossfader", {{"value", value}}, "osc"};
  } else if (path == "/prismforge/blackout" ||
             path == "/prismforge/panic-dim") {
    const auto enabled = OscBoolean(message, 0);
    if (args.size() != 1 || !enabled) return false;
    command = {path.ends_with("blackout") ? "setBlackout" : "setPanicDim",
               {{"enabled", *enabled}}, "osc"};
  } else if (path == "/prismforge/deck/A/scene" ||
             path == "/prismforge/deck/B/scene") {
    const auto id = Argument<std::string>(message, 0);
    if (args.size() != 1 || !id || id->empty() || id->size() > 64) return false;
    command = {"setScene", {{"deck", path[17] == 'A' ? "A" : "B"},
                             {"sceneId", *id}}, "osc"};
  } else if (path.starts_with("/prismforge/deck/") &&
             path.find("/effect/") != std::string::npos) {
    constexpr std::string_view prefix = "/prismforge/deck/";
    constexpr std::string_view suffix = "/effect/";
    if (path.size() != prefix.size() + 1 + suffix.size() + 1 ||
        (path[prefix.size()] != 'A' && path[prefix.size()] != 'B') ||
        path.compare(prefix.size() + 1, suffix.size(), suffix) != 0 ||
        path.back() < '0' || path.back() > '3') return true;
    float amount = 0.0f;
    if (args.size() != 1 || !UnitFloat(message, 0, amount)) return false;
    command = {"setEffect", {{"deck", path[prefix.size()] == 'A' ? "A" : "B"},
                              {"effectIndex", path.back() - '0'},
                              {"amount", amount}}, "osc"};
  } else if (path == "/prismforge/cue/save" ||
             path == "/prismforge/cue/recall") {
    const auto index = Argument<std::int32_t>(message, 0);
    if (!index || *index < 0 || *index >= 32 ||
        (path.ends_with("save") && args.size() != 1) ||
        (path.ends_with("recall") && args.size() != 1 && args.size() != 2)) return false;
    if (path.ends_with("save")) {
      command = {"saveCue", {{"index", *index}}, "osc"};
    } else {
      std::string quantization = "immediate";
      if (args.size() == 2) {
        const auto value = Argument<std::string>(message, 1);
        if (!value || (*value != "immediate" && *value != "beat" &&
                       *value != "bar")) return false;
        quantization = *value;
      }
      command = {"recallCue", {{"index", *index},
                               {"quantization", quantization}}, "osc"};
    }
  } else if (path == "/prismforge/request-snapshot") {
    if (!args.empty()) return false;
    command = {"requestSnapshot", nlohmann::json::object(), "osc"};
  } else {
    return true;  // Valid OSC message with an unknown or reserved address.
  }
  output.commands.push_back(std::move(command));
  return true;
}

void AppendBigEndian32(std::vector<std::byte>& bytes, std::uint32_t value) {
  bytes.push_back(std::byte(value >> 24));
  bytes.push_back(std::byte(value >> 16));
  bytes.push_back(std::byte(value >> 8));
  bytes.push_back(std::byte(value));
}

void AppendOscString(std::vector<std::byte>& bytes, std::string_view value) {
  for (const char ch : value) bytes.push_back(std::byte(static_cast<unsigned char>(ch)));
  bytes.push_back(std::byte{0});
  while (bytes.size() % 4 != 0) bytes.push_back(std::byte{0});
}

SOCKET BindLoopbackSocket(std::uint16_t port, std::string& error) {
  SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket == INVALID_SOCKET) {
    error = "OSC UDP socket: " + std::to_string(WSAGetLastError());
    return INVALID_SOCKET;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
    error = "OSC UDP loopback bind on port " + std::to_string(port) + ": " +
            std::to_string(WSAGetLastError());
    closesocket(socket);
    return INVALID_SOCKET;
  }
  return socket;
}

std::filesystem::path PrismBurstStatusPath() {
  const DWORD length = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (length == 0 || length > 32768) return {};
  std::wstring path(length, L'\0');
  if (GetEnvironmentVariableW(L"APPDATA", path.data(), length) != length - 1) return {};
  path.resize(length - 1);
  return std::filesystem::path(path) / "PrismBurst" / "status.json";
}

template <typename T>
void CopyFieldIf(const nlohmann::json& source, nlohmann::json& destination,
                 const char* name) {
  if (!source.contains(name)) return;
  try {
    const T value = source.at(name).get<T>();
    if constexpr (std::is_floating_point_v<T>) {
      if (!std::isfinite(value)) return;
    }
    if constexpr (std::is_same_v<T, std::string>) {
      if (value.size() > kMaxStringBytes) return;
    }
    destination[name] = value;
  } catch (const nlohmann::json::exception&) {
  }
}
}  // namespace

bool DecodeOscDatagram(std::span<const std::byte> packet,
                       std::uint16_t destinationPort,
                       DecodedOscPacket& output) {
  std::vector<OscMessage> messages;
  DecodedOscPacket decoded;
  if (!ParseMessages(packet, 0, messages)) return false;
  for (const auto& message : messages) {
    if (!DecodeRecognized(message, destinationPort, decoded)) return false;
  }
  output = std::move(decoded);
  return true;
}

struct OscBridge::Impl {
  Impl(BoundedQueue<QueuedCommand>& commandQueue,
       BoundedQueue<GestureEvent>& gestureQueue)
      : commands(commandQueue), gestures(gestureQueue) {}

  void Run() {
    std::array<std::byte, kMaxPacketBytes + 1> bytes{};
    while (!stopping) {
      fd_set readers;
      FD_ZERO(&readers);
      FD_SET(commandSocket, &readers);
      FD_SET(gestureSocket, &readers);
      timeval timeout{0, 100000};
      const int ready = select(0, &readers, nullptr, nullptr, &timeout);
      if (ready == SOCKET_ERROR) {
        ++rejectedPackets;
        continue;
      }
      if (ready == 0) continue;
      for (const auto [socket, port] :
           {std::pair{commandSocket, OscBridge::kCommandPort},
            std::pair{gestureSocket, OscBridge::kGesturePort}}) {
        if (!FD_ISSET(socket, &readers)) continue;
        sockaddr_in sender{};
        int senderLength = sizeof(sender);
        const int length = recvfrom(socket, reinterpret_cast<char*>(bytes.data()),
                                    static_cast<int>(bytes.size()), 0,
                                    reinterpret_cast<sockaddr*>(&sender), &senderLength);
        if (length <= 0 || length > static_cast<int>(kMaxPacketBytes) ||
            sender.sin_family != AF_INET ||
            sender.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
          ++rejectedPackets;
          continue;
        }
        DecodedOscPacket decoded;
        if (!DecodeOscDatagram(std::span(bytes.data(), static_cast<std::size_t>(length)),
                               port, decoded)) {
          ++rejectedPackets;
          continue;
        }
        for (auto& command : decoded.commands) {
          if (!commands.TryPush(std::move(command))) ++droppedCommands;
        }
        for (auto& gesture : decoded.gestures) {
          if (!gestures.TryPush(std::move(gesture))) ++droppedGestures;
        }
      }
    }
  }

  BoundedQueue<QueuedCommand>& commands;
  BoundedQueue<GestureEvent>& gestures;
  std::thread worker;
  std::atomic_bool stopping{false};
  std::atomic<std::uint64_t> droppedCommands{0};
  std::atomic<std::uint64_t> droppedGestures{0};
  std::atomic<std::uint64_t> rejectedPackets{0};
  SOCKET commandSocket = INVALID_SOCKET;
  SOCKET gestureSocket = INVALID_SOCKET;
  SOCKET statusSocket = INVALID_SOCKET;
  bool winsockStarted = false;
};

OscBridge::OscBridge(BoundedQueue<QueuedCommand>& commands,
                     BoundedQueue<GestureEvent>& gestures)
    : impl_(std::make_unique<Impl>(commands, gestures)) {}
OscBridge::~OscBridge() { Stop(); }

bool OscBridge::Start(std::string& error) {
  error.clear();
  if (impl_->worker.joinable()) return true;
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    error = "OSC WSAStartup failed";
    return false;
  }
  impl_->winsockStarted = true;
  impl_->commandSocket = BindLoopbackSocket(kCommandPort, error);
  if (impl_->commandSocket == INVALID_SOCKET) { Stop(); return false; }
  impl_->gestureSocket = BindLoopbackSocket(kGesturePort, error);
  if (impl_->gestureSocket == INVALID_SOCKET) { Stop(); return false; }
  impl_->statusSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (impl_->statusSocket == INVALID_SOCKET) {
    error = "OSC status socket: " + std::to_string(WSAGetLastError());
    Stop();
    return false;
  }
  u_long nonblocking = 1;
  if (ioctlsocket(impl_->statusSocket, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    error = "OSC status socket nonblocking mode failed";
    Stop();
    return false;
  }
  impl_->stopping = false;
  impl_->worker = std::thread([this] { impl_->Run(); });
  return true;
}

void OscBridge::Stop() {
  impl_->stopping = true;
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->commandSocket != INVALID_SOCKET) {
    closesocket(impl_->commandSocket);
    impl_->commandSocket = INVALID_SOCKET;
  }
  if (impl_->gestureSocket != INVALID_SOCKET) {
    closesocket(impl_->gestureSocket);
    impl_->gestureSocket = INVALID_SOCKET;
  }
  if (impl_->statusSocket != INVALID_SOCKET) {
    closesocket(impl_->statusSocket);
    impl_->statusSocket = INVALID_SOCKET;
  }
  if (impl_->winsockStarted) {
    WSACleanup();
    impl_->winsockStarted = false;
  }
}

bool OscBridge::SendStatus(float fps, std::int32_t qualityTier, float rms,
                           bool spoutReady) noexcept {
  if (impl_->statusSocket == INVALID_SOCKET || !std::isfinite(fps) ||
      !std::isfinite(rms)) return false;
  try {
    std::vector<std::byte> packet;
    packet.reserve(64);
    AppendOscString(packet, "/prismforge/status");
    AppendOscString(packet, ",fifi");
    AppendBigEndian32(packet, std::bit_cast<std::uint32_t>(fps));
    AppendBigEndian32(packet, std::bit_cast<std::uint32_t>(qualityTier));
    AppendBigEndian32(packet, std::bit_cast<std::uint32_t>(rms));
    AppendBigEndian32(packet, spoutReady ? 1 : 0);
    sockaddr_in receiver{};
    receiver.sin_family = AF_INET;
    receiver.sin_port = htons(kStatusPort);
    receiver.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return sendto(impl_->statusSocket, reinterpret_cast<const char*>(packet.data()),
                  static_cast<int>(packet.size()), 0,
                  reinterpret_cast<const sockaddr*>(&receiver), sizeof(receiver)) ==
           static_cast<int>(packet.size());
  } catch (...) {
    return false;
  }
}

nlohmann::json OscBridge::ReadPrismBurstHealth() {
  nlohmann::json health = {{"available", false}, {"fresh", false}};
  try {
    const auto path = PrismBurstStatusPath();
    if (path.empty() || !std::filesystem::is_regular_file(path)) return health;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return health;
    std::string content(128 * 1024 + 1, '\0');
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    const auto length = stream.gcount();
    if (length <= 0 || length > 128 * 1024) return health;
    content.resize(static_cast<std::size_t>(length));
    const auto status = nlohmann::json::parse(content, nullptr, false);
    if (status.is_discarded() || !status.is_object()) return health;
    health["available"] = true;
    CopyFieldIf<std::int64_t>(status, health, "updatedAtEpochMs");
    CopyFieldIf<float>(status, health, "fps");
    CopyFieldIf<std::int32_t>(status, health, "targetFps");
    CopyFieldIf<bool>(status, health, "spoutReady");
    CopyFieldIf<std::string>(status, health, "spoutSenderName");
    CopyFieldIf<bool>(status, health, "audioReady");
    CopyFieldIf<std::string>(status, health, "kinectStatus");
    CopyFieldIf<float>(status, health, "trackingConfidence");
    CopyFieldIf<std::int32_t>(status, health, "bodyUserCount");
    CopyFieldIf<std::int64_t>(status, health, "gestureEventCount");
    CopyFieldIf<std::string>(status, health, "lastGesture");
    if (health.contains("updatedAtEpochMs")) {
      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      const auto age = now - health.at("updatedAtEpochMs").get<std::int64_t>();
      health["fresh"] = age >= 0 && age <= 5000;
    }
  } catch (const std::exception&) {
    health = {{"available", false}, {"fresh", false}};
  }
  return health;
}

std::uint64_t OscBridge::DroppedCommands() const noexcept {
  return impl_->droppedCommands.load();
}
std::uint64_t OscBridge::DroppedGestures() const noexcept {
  return impl_->droppedGestures.load();
}
std::uint64_t OscBridge::RejectedPackets() const noexcept {
  return impl_->rejectedPackets.load();
}

}  // namespace prismforge
