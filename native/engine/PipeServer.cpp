#include "PipeServer.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>

namespace prismforge {
namespace {
constexpr wchar_t kPipeName[] = LR"(\\.\pipe\PrismForge.v1)";
constexpr std::uint32_t kMaxMessageBytes = 65536;

bool ReadExact(HANDLE pipe, void* destination, DWORD bytes) {
  auto* cursor = static_cast<std::uint8_t*>(destination);
  while (bytes) {
    DWORD read = 0;
    if (!ReadFile(pipe, cursor, bytes, &read, nullptr) || read == 0) return false;
    cursor += read;
    bytes -= read;
  }
  return true;
}
}  // namespace

PipeServer::PipeServer(BoundedQueue<QueuedCommand>& commands) : commands_(commands) {}
PipeServer::~PipeServer() { Stop(); }

void PipeServer::Start() {
  if (worker_.joinable()) return;
  stopping_ = false;
  worker_ = std::thread([this] { Run(); });
}

void PipeServer::Stop() {
  stopping_ = true;
  if (worker_.joinable()) {
    CancelSynchronousIo(worker_.native_handle());
    worker_.join();
  }
  connected_ = false;
}

void PipeServer::Publish(nlohmann::json snapshot, nlohmann::json signal) {
  std::lock_guard lock(latestMutex_);
  latestSnapshot_ = std::move(snapshot);
  latestSignal_ = std::move(signal);
}

void PipeServer::ReportError(std::string message, std::string requestId) {
  if (!connected_) return;
  (void)errors_.TryPush({{"message", std::move(message)},
                         {"requestId", std::move(requestId)}});
}

bool PipeServer::Send(void* opaquePipe, const char* type,
                      const nlohmann::json& payload) {
  const auto packet = nlohmann::json{
      {"version", 1}, {"type", type}, {"payload", payload}}.dump();
  if (packet.size() > kMaxMessageBytes) return false;
  const auto length = static_cast<std::uint32_t>(packet.size());
  std::array<std::uint8_t, 4> header = {
      static_cast<std::uint8_t>(length),
      static_cast<std::uint8_t>(length >> 8),
      static_cast<std::uint8_t>(length >> 16),
      static_cast<std::uint8_t>(length >> 24)};
  HANDLE pipe = static_cast<HANDLE>(opaquePipe);
  DWORD written = 0;
  if (!WriteFile(pipe, header.data(), static_cast<DWORD>(header.size()),
                 &written, nullptr) || written != header.size()) return false;
  if (!WriteFile(pipe, packet.data(), length, &written, nullptr) ||
      written != length) return false;
  return true;
}

void PipeServer::Run() {
  using Clock = std::chrono::steady_clock;
  while (!stopping_) {
    HANDLE pipe = CreateNamedPipeW(
        kPipeName, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, kMaxMessageBytes, kMaxMessageBytes, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }
    const BOOL accepted = ConnectNamedPipe(pipe, nullptr) ? TRUE :
        (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!accepted || stopping_) {
      CloseHandle(pipe);
      continue;
    }
    connected_ = true;
    nlohmann::json snapshot;
    nlohmann::json signal;
    {
      std::lock_guard lock(latestMutex_);
      snapshot = latestSnapshot_;
      signal = latestSignal_;
    }
    // A client can connect before the first render tick publishes state. Do
    // not emit an empty payload under the otherwise stable v1 snapshot type.
    bool alive = snapshot.is_null() || Send(pipe, "StateSnapshot", snapshot);
    auto lastSnapshot = Clock::now();
    auto lastSignal = Clock::now();
    while (alive && !stopping_) {
      DWORD available = 0;
      if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break;
      if (available >= 4) {
        std::array<std::uint8_t, 4> header{};
        if (!ReadExact(pipe, header.data(), 4)) break;
        const std::uint32_t length =
            std::uint32_t(header[0]) | (std::uint32_t(header[1]) << 8) |
            (std::uint32_t(header[2]) << 16) | (std::uint32_t(header[3]) << 24);
        if (length == 0 || length > kMaxMessageBytes) {
          alive = Send(pipe, "ErrorEvent", {{"message", "Invalid IPC length"}});
          break;
        }
        std::string message(length, '\0');
        if (!ReadExact(pipe, message.data(), length)) break;
        try {
          const auto envelope = nlohmann::json::parse(message);
          if (envelope.at("version") != 1 || envelope.at("type") != "Command" ||
              !envelope.at("payload").is_object()) {
            alive = Send(pipe, "ErrorEvent", {{"message", "Unsupported IPC envelope"}});
          } else {
            const auto& payload = envelope.at("payload");
            QueuedCommand command;
            command.name = payload.at("action").get<std::string>();
            command.payload = payload;
            command.requestId = envelope.value("requestId", std::string{});
            if (!commands_.TryPush(std::move(command))) {
              alive = Send(pipe, "ErrorEvent", {{"message", "Command queue full"}});
            }
          }
        } catch (const std::exception&) {
          alive = Send(pipe, "ErrorEvent", {{"message", "Malformed IPC command"}});
        }
      }
      const auto now = Clock::now();
      while (alive) {
        auto error = errors_.TryPop();
        if (!error) break;
        alive = Send(pipe, "ErrorEvent", *error);
      }
      if (now - lastSignal >= std::chrono::milliseconds(50)) {
        {
          std::lock_guard lock(latestMutex_);
          signal = latestSignal_;
        }
        lastSignal = now;
        if (!signal.is_null()) alive = Send(pipe, "SignalFrame", signal);
      }
      if (alive && now - lastSnapshot >= std::chrono::milliseconds(250)) {
        {
          std::lock_guard lock(latestMutex_);
          snapshot = latestSnapshot_;
        }
        lastSnapshot = now;
        if (!snapshot.is_null()) alive = Send(pipe, "StateSnapshot", snapshot);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    connected_ = false;
    while (errors_.TryPop()) {}
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
}

}  // namespace prismforge
