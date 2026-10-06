#include "AudioSwitcher.h"

#include <chrono>
#include <exception>
#include <utility>

namespace prismforge {

namespace {
bool ValidSourceId(const std::string& sourceId) {
  if (sourceId == "system-default") return true;
  constexpr std::string_view input = "input:wasapi:";
  constexpr std::string_view loopback = "loopback:wasapi:";
  std::string_view encoded;
  if (sourceId.starts_with(input)) {
    encoded = std::string_view(sourceId).substr(input.size());
  } else if (sourceId.starts_with(loopback)) {
    encoded = std::string_view(sourceId).substr(loopback.size());
  } else {
    return false;
  }
  if (encoded.empty() || encoded.size() > 63 * 4 || encoded.size() % 4 != 0) {
    return false;
  }
  for (const char ch : encoded) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return false;
  }
  return true;
}
}  // namespace

AudioSwitcher::AudioSwitcher(BoundedQueue<AudioBlock>& samples,
                             CaptureFactory factory)
    : samples_(samples), factory_(std::move(factory)) {
  if (!factory_) {
    factory_ = [](BoundedQueue<AudioBlock>& queue) {
      return std::make_unique<AudioCapture>(queue, false);
    };
  }
}

AudioSwitcher::~AudioSwitcher() { Stop(); }

bool AudioSwitcher::Start(bool captureEnabled, std::string& error) {
  if (worker_.joinable()) {
    error = "Audio switch worker is already running";
    return false;
  }
  stopping_.store(false);
  connected_.store(false);
  sampleRate_.store(48000);
  try {
    worker_ = std::thread(&AudioSwitcher::Worker, this, captureEnabled);
  } catch (const std::exception& exception) {
    error = std::string("Cannot start audio worker: ") + exception.what();
    return false;
  }
  return true;
}

bool AudioSwitcher::Request(const std::string& sourceId, std::string& error) {
  if (!worker_.joinable() || stopping_.load()) {
    error = "Audio switch worker is unavailable";
    return false;
  }
  if (!ValidSourceId(sourceId)) {
    error = "Audio source ID is invalid or uses a retired positional identity";
    return false;
  }
  if (!requests_.TryPush(sourceId)) {
    error = "Audio switch queue is full";
    return false;
  }
  return true;
}

std::optional<AudioSwitchEvent> AudioSwitcher::Poll() { return events_.TryPop(); }

void AudioSwitcher::Stop() {
  stopping_.store(true);
  if (worker_.joinable()) worker_.join();
  connected_.store(false);
}

void AudioSwitcher::Publish(const AudioSwitchEvent& event) {
  while (!stopping_.load()) {
    if (events_.TryPush(event)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void AudioSwitcher::SwitchTo(const std::string& sourceId,
                             std::unique_ptr<IAudioCapture>& active,
                             std::string& activeId) {
  if (sourceId == activeId && active && active->IsRunning()) {
    connected_.store(true);
    Publish({.kind = AudioSwitchEventKind::Switched,
             .requestedId = sourceId, .activeId = activeId,
             .connected = true, .sampleRate = active->SampleRate()});
    return;
  }

  auto candidate = factory_(samples_);
  candidate->SetActive(false);
  const bool loopback = sourceId == "system-default" ||
                        sourceId.starts_with("loopback:wasapi:");
  std::string error;
  const bool started = candidate->Start(
      loopback, sourceId == "system-default" ? "" : sourceId, error);
  std::string listError;
  auto devices = candidate->Devices(listError);
  const bool hasDevices = listError.empty();

  if (!started || !candidate->IsRunning()) {
    if (error.empty()) error = "Requested capture did not start";
    const bool stillConnected = active && active->IsRunning();
    Publish({.kind = AudioSwitchEventKind::Failed,
             .requestedId = sourceId, .activeId = activeId,
             .devices = std::move(devices), .error = std::move(error),
             .hasDevices = hasDevices,
             .connected = stillConnected,
             .sampleRate = active ? active->SampleRate() : 48000});
    return;
  }

  // The candidate has actually started, but its callback remains gated until
  // the old stream is gated off. No mixed old/new sample blocks are published.
  if (active) active->SetActive(false);
  candidate->SetActive(true);
  auto previous = std::move(active);
  active = std::move(candidate);
  activeId = sourceId;
  connected_.store(true);
  sampleRate_.store(active->SampleRate());
  Publish({.kind = AudioSwitchEventKind::Switched,
           .requestedId = sourceId, .activeId = activeId,
           .devices = std::move(devices), .hasDevices = hasDevices,
           .connected = true, .sampleRate = active->SampleRate()});
  // Device teardown can stall, so it is always performed on the worker.
  if (previous) previous->Stop();
}

void AudioSwitcher::Worker(bool captureEnabled) {
  std::unique_ptr<IAudioCapture> active;
  std::string activeId;
  {
    auto probe = factory_(samples_);
    std::string error;
    auto devices = probe->Devices(error);
    const bool hasDevices = error.empty();
    Publish({.kind = AudioSwitchEventKind::Sources,
             .devices = std::move(devices), .error = std::move(error),
             .hasDevices = hasDevices, .connected = false});
  }
  if (captureEnabled && !stopping_.load()) {
    SwitchTo("system-default", active, activeId);
  }
  const auto reconcileActive = [&] {
    if (!active) return;
    const bool running = active->IsRunning();
    const bool wasConnected = connected_.exchange(running);
    if (wasConnected && !running) {
      Publish({.kind = AudioSwitchEventKind::Disconnected,
               .activeId = activeId,
               .error = "Active audio endpoint stopped or was interrupted",
               .sampleRate = active->SampleRate()});
    } else if (!wasConnected && running) {
      // An interrupted WASAPI stream may resume without a new source request.
      // Re-announce it once so the render thread can reset its sample clock.
      Publish({.kind = AudioSwitchEventKind::Switched,
               .requestedId = activeId, .activeId = activeId,
               .connected = true, .sampleRate = active->SampleRate()});
    }
  };
  while (!stopping_.load()) {
    if (auto request = requests_.TryPop()) {
      SwitchTo(*request, active, activeId);
      reconcileActive();
      continue;
    }
    reconcileActive();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (active) active->Stop();
  connected_.store(false);
}

}  // namespace prismforge
