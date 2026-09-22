#pragma once

#include "PrismForge/BoundedQueue.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace prismforge {

struct QueuedCommand {
  std::string name;
  nlohmann::json payload;
  std::string requestId;
};

class PipeServer {
 public:
  explicit PipeServer(BoundedQueue<QueuedCommand>& commands);
  ~PipeServer();
  PipeServer(const PipeServer&) = delete;
  PipeServer& operator=(const PipeServer&) = delete;
  void Start();
  void Stop();
  void Publish(nlohmann::json snapshot, nlohmann::json signal);
  void ReportError(std::string message, std::string requestId = {});
  [[nodiscard]] bool ClientConnected() const noexcept { return connected_.load(); }

 private:
  void Run();
  bool Send(void* pipe, const char* type, const nlohmann::json& payload);
  BoundedQueue<QueuedCommand>& commands_;
  std::thread worker_;
  std::atomic_bool stopping_{false};
  std::atomic_bool connected_{false};
  std::mutex latestMutex_;
  nlohmann::json latestSnapshot_ = nlohmann::json::object();
  nlohmann::json latestSignal_ = nlohmann::json::object();
  BoundedQueue<nlohmann::json> errors_{64};
};

}  // namespace prismforge
