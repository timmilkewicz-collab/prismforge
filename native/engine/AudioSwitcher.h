#pragma once

#include "AudioCapture.h"

#include "PrismForge/BoundedQueue.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace prismforge {

enum class AudioSwitchEventKind { Sources, Switched, Failed, Disconnected };

struct AudioSwitchEvent {
  AudioSwitchEventKind kind = AudioSwitchEventKind::Sources;
  std::string requestedId;
  std::string activeId;
  std::vector<AudioDeviceInfo> devices;
  std::string error;
  bool hasDevices = false;
  bool connected = false;
  unsigned sampleRate = 48000;
};

// Only the worker owns or touches WASAPI/miniaudio objects. The render thread
// uses bounded, nonblocking queues and reads atomics for status.
class AudioSwitcher {
 public:
  using CaptureFactory =
      std::function<std::unique_ptr<IAudioCapture>(BoundedQueue<AudioBlock>&)>;

  explicit AudioSwitcher(BoundedQueue<AudioBlock>& samples,
                         CaptureFactory factory = {});
  ~AudioSwitcher();
  AudioSwitcher(const AudioSwitcher&) = delete;
  AudioSwitcher& operator=(const AudioSwitcher&) = delete;

  // Starts the worker only; enumeration and default capture are asynchronous.
  bool Start(bool captureEnabled, std::string& error);
  // Returns false immediately on a malformed ID or a full request queue.
  bool Request(const std::string& sourceId, std::string& error);
  [[nodiscard]] std::optional<AudioSwitchEvent> Poll();
  [[nodiscard]] bool IsRunning() const noexcept { return connected_.load(); }
  [[nodiscard]] unsigned SampleRate() const noexcept { return sampleRate_.load(); }
  // May wait on WASAPI and must only be called after leaving the render loop.
  void Stop();

 private:
  void Worker(bool captureEnabled);
  void SwitchTo(const std::string& sourceId,
                std::unique_ptr<IAudioCapture>& active,
                std::string& activeId);
  void Publish(const AudioSwitchEvent& event);

  BoundedQueue<AudioBlock>& samples_;
  CaptureFactory factory_;
  BoundedQueue<std::string> requests_{4};
  BoundedQueue<AudioSwitchEvent> events_{8};
  std::thread worker_;
  std::atomic_bool stopping_{false};
  std::atomic_bool connected_{false};
  std::atomic_uint sampleRate_{48000};
};

}  // namespace prismforge
