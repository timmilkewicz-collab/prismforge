#pragma once

#include "PrismForge/BoundedQueue.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <miniaudio.h>

namespace prismforge {

struct AudioBlock {
  std::array<float, 512> samples{};
  std::size_t count = 0;
};

struct AudioDeviceInfo {
  std::string id;
  std::string name;
  bool loopback = false;
};

class AudioCapture {
 public:
  explicit AudioCapture(BoundedQueue<AudioBlock>& queue);
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  bool Start(bool loopback, const std::string& deviceName, std::string& error);
  void Stop();
  [[nodiscard]] std::vector<AudioDeviceInfo> Devices(std::string& error);
  [[nodiscard]] bool IsRunning() const noexcept { return running_; }
  [[nodiscard]] unsigned SampleRate() const noexcept { return sampleRate_; }

 private:
  static void DataCallback(ma_device* device, void* output, const void* input,
                           ma_uint32 frameCount);
  BoundedQueue<AudioBlock>& queue_;
  ma_context context_{};
  ma_device device_{};
  bool contextReady_ = false;
  bool deviceReady_ = false;
  bool running_ = false;
  unsigned sampleRate_ = 48000;
};

}  // namespace prismforge
