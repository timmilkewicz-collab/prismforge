#pragma once

#include "PrismForge/ShowState.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace prismforge {

// Converts the authoritative render-thread show state into immutable save jobs.
// Disk I/O is performed on a worker, never on the render or audio callback.
class ShowStore {
 public:
  ShowStore();
  ~ShowStore();
  ShowStore(const ShowStore&) = delete;
  ShowStore& operator=(const ShowStore&) = delete;

  [[nodiscard]] bool Ready() const noexcept { return !autosavePath_.empty(); }
  [[nodiscard]] const std::filesystem::path& AutosavePath() const noexcept {
    return autosavePath_;
  }
  bool LoadAutosave(ShowState& show, std::string& error) const;
  bool LoadPortable(ShowState& show, const std::string& name,
                    std::string& error) const;
  bool RequestAutosave(const ShowState& show);
  bool RequestPortableSave(const ShowState& show, const std::string& name,
                           std::string& error);
  void Stop();

 private:
  struct SaveJob {
    std::filesystem::path path;
    nlohmann::json bundle;
  };
  void Worker();
  bool Queue(SaveJob job);
  std::filesystem::path autosavePath_;
  std::filesystem::path showsDirectory_;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable signal_;
  std::deque<SaveJob> pending_;
  bool stopping_ = false;
};

}  // namespace prismforge
