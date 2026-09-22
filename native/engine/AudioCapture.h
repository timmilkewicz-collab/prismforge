#pragma once

#include "PrismForge/BoundedQueue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
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

namespace detail {
// All operations are seq-cst: a callback that observed active=true has already
// incremented inFlight before DeactivateAndDrain can observe zero. Callbacks
// entering afterward observe active=false and cannot publish samples.
class AudioCallbackGate {
 public:
  explicit AudioCallbackGate(bool active) noexcept : active_(active) {}
  [[nodiscard]] bool Enter() noexcept;
  void Exit() noexcept;
  void Activate() noexcept;
  void DeactivateAndDrain() noexcept;
  [[nodiscard]] bool Active() const noexcept { return active_.load(); }

 private:
  std::atomic_bool active_;
  std::atomic_uint inFlight_{0};
};
}  // namespace detail

// An opaque, stable WASAPI endpoint key. Never use enumeration positions or
// friendly names as control identities: both can silently remap on hotplug.
[[nodiscard]] std::string WasapiSourceId(bool loopback,
                                         std::wstring_view endpointId);

class IAudioCapture {
 public:
  virtual ~IAudioCapture() = default;
  virtual bool Start(bool loopback, const std::string& sourceId,
                     std::string& error) = 0;
  virtual void Stop() = 0;
  virtual std::vector<AudioDeviceInfo> Devices(std::string& error) = 0;
  virtual void SetActive(bool active) noexcept = 0;
  [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
  [[nodiscard]] virtual unsigned SampleRate() const noexcept = 0;
};

class AudioCapture final : public IAudioCapture {
 public:
  explicit AudioCapture(BoundedQueue<AudioBlock>& queue, bool active = true);
  ~AudioCapture() override;
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  bool Start(bool loopback, const std::string& sourceId,
             std::string& error) override;
  void Stop() override;
  [[nodiscard]] std::vector<AudioDeviceInfo> Devices(std::string& error) override;
  void SetActive(bool active) noexcept override {
    if (active) callbackGate_.Activate();
    else callbackGate_.DeactivateAndDrain();
  }
  [[nodiscard]] bool IsRunning() const noexcept override { return running_.load(); }
  [[nodiscard]] unsigned SampleRate() const noexcept override { return sampleRate_; }

 private:
  static void DataCallback(ma_device* device, void* output, const void* input,
                           ma_uint32 frameCount);
  static void NotificationCallback(const ma_device_notification* notification);
  BoundedQueue<AudioBlock>& queue_;
  ma_context context_{};
  ma_device device_{};
  bool contextReady_ = false;
  bool deviceReady_ = false;
  detail::AudioCallbackGate callbackGate_{true};
  std::atomic_bool running_{false};
  unsigned sampleRate_ = 48000;
};

}  // namespace prismforge
