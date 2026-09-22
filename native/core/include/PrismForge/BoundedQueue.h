#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace prismforge {

// Producers, including audio/MIDI callbacks, never wait for the consumer.
template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : entries_(capacity) {}

  bool TryPush(T value) {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || size_ == entries_.size() || entries_.empty()) {
      return false;
    }
    entries_[tail_].emplace(std::move(value));
    tail_ = (tail_ + 1) % entries_.size();
    ++size_;
    return true;
  }

  std::optional<T> TryPop() {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || size_ == 0) {
      return std::nullopt;
    }
    T value = std::move(*entries_[head_]);
    entries_[head_].reset();
    head_ = (head_ + 1) % entries_.size();
    --size_;
    return value;
  }

 private:
  std::mutex mutex_;
  std::vector<std::optional<T>> entries_;
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::size_t size_ = 0;
};

}  // namespace prismforge
