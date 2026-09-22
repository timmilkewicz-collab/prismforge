#define MA_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "AudioCapture.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <thread>
#include <utility>

namespace prismforge {

bool detail::AudioCallbackGate::Enter() noexcept {
  inFlight_.fetch_add(1, std::memory_order_seq_cst);
  if (!active_.load(std::memory_order_seq_cst)) {
    Exit();
    return false;
  }
  return true;
}

void detail::AudioCallbackGate::Exit() noexcept {
  inFlight_.fetch_sub(1, std::memory_order_seq_cst);
}

void detail::AudioCallbackGate::Activate() noexcept {
  active_.store(true, std::memory_order_seq_cst);
}

void detail::AudioCallbackGate::DeactivateAndDrain() noexcept {
  active_.store(false, std::memory_order_seq_cst);
  while (inFlight_.load(std::memory_order_seq_cst) != 0) {
    std::this_thread::yield();
  }
}

namespace {
std::wstring_view EndpointString(const ma_device_id& id) {
  std::size_t length = 0;
  while (length < std::size(id.wasapi) && id.wasapi[length] != 0) ++length;
  if (length == 0 || length == std::size(id.wasapi)) return {};
  return {id.wasapi, length};
}
}  // namespace

std::string WasapiSourceId(bool loopback, std::wstring_view endpointId) {
  if (endpointId.empty() || endpointId.size() >= 64) return {};
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string result = loopback ? "loopback:wasapi:" : "input:wasapi:";
  result.reserve(result.size() + endpointId.size() * 4);
  for (wchar_t ch : endpointId) {
    if (ch == 0 || static_cast<std::uint32_t>(ch) > 0xFFFF) return {};
    const auto code = static_cast<std::uint16_t>(ch);
    result.push_back(hex[(code >> 12) & 0xF]);
    result.push_back(hex[(code >> 8) & 0xF]);
    result.push_back(hex[(code >> 4) & 0xF]);
    result.push_back(hex[code & 0xF]);
  }
  return result;
}

AudioCapture::AudioCapture(BoundedQueue<AudioBlock>& queue, bool active)
    : queue_(queue), callbackGate_(active) {}
AudioCapture::~AudioCapture() {
  Stop();
  if (contextReady_) ma_context_uninit(&context_);
}

std::vector<AudioDeviceInfo> AudioCapture::Devices(std::string& error) {
  error.clear();
  std::vector<AudioDeviceInfo> result;
  if (!contextReady_) {
    const ma_backend backend = ma_backend_wasapi;
    const ma_result status = ma_context_init(&backend, 1, nullptr, &context_);
    if (status != MA_SUCCESS) {
      error = std::string("WASAPI initialization: ") + ma_result_description(status);
      return result;
    }
    contextReady_ = true;
  }
  ma_device_info* playback = nullptr;
  ma_device_info* capture = nullptr;
  ma_uint32 playbackCount = 0;
  ma_uint32 captureCount = 0;
  const ma_result status = ma_context_get_devices(
      &context_, &playback, &playbackCount, &capture, &captureCount);
  if (status != MA_SUCCESS) {
    error = std::string("Audio enumeration: ") + ma_result_description(status);
    return result;
  }
  for (ma_uint32 i = 0; i < playbackCount; ++i) {
    const auto id = WasapiSourceId(true, EndpointString(playback[i].id));
    if (!id.empty()) result.push_back({id, playback[i].name, true});
  }
  for (ma_uint32 i = 0; i < captureCount; ++i) {
    const auto id = WasapiSourceId(false, EndpointString(capture[i].id));
    if (!id.empty()) result.push_back({id, capture[i].name, false});
  }
  return result;
}

bool AudioCapture::Start(bool loopback, const std::string& sourceId,
                         std::string& error) {
  error.clear();
  Stop();
  (void)Devices(error);
  if (!contextReady_ || !error.empty()) return false;
  ma_device_info* playback = nullptr;
  ma_device_info* capture = nullptr;
  ma_uint32 playbackCount = 0;
  ma_uint32 captureCount = 0;
  const ma_result listed = ma_context_get_devices(
      &context_, &playback, &playbackCount, &capture, &captureCount);
  if (listed != MA_SUCCESS) {
    error = std::string("Audio enumeration: ") + ma_result_description(listed);
    return false;
  }

  ma_device_config config = ma_device_config_init(
      loopback ? ma_device_type_loopback : ma_device_type_capture);
  config.capture.format = ma_format_f32;
  config.capture.channels = 1;
  config.sampleRate = 48000;
  config.dataCallback = DataCallback;
  config.notificationCallback = NotificationCallback;
  config.pUserData = this;
  ma_device_id selectedId{};
  if (!sourceId.empty()) {
    const auto* devices = loopback ? playback : capture;
    const ma_uint32 count = loopback ? playbackCount : captureCount;
    bool selected = false;
    for (ma_uint32 i = 0; i < count; ++i) {
      if (sourceId == WasapiSourceId(loopback, EndpointString(devices[i].id))) {
        if (selected) {
          error = "Requested WASAPI endpoint identity is ambiguous";
          return false;
        }
        selectedId = devices[i].id;
        selected = true;
      }
    }
    if (!selected) {
      error = "Requested WASAPI endpoint is no longer present";
      return false;
    }
    config.capture.pDeviceID = &selectedId;
  }

  ma_result status = ma_device_init(&context_, &config, &device_);
  if (status != MA_SUCCESS) {
    error = std::string("Audio device initialization: ") + ma_result_description(status);
    return false;
  }
  deviceReady_ = true;
  if (!sourceId.empty() &&
      WasapiSourceId(loopback, EndpointString(device_.capture.id)) != sourceId) {
    error = "WASAPI endpoint changed while opening; capture rejected";
    Stop();
    return false;
  }
  sampleRate_ = device_.sampleRate;
  status = ma_device_start(&device_);
  if (status != MA_SUCCESS) {
    error = std::string("Audio device start: ") + ma_result_description(status);
    Stop();
    return false;
  }
  running_.store(true);
  return true;
}

void AudioCapture::Stop() {
  running_.store(false);
  if (deviceReady_) {
    ma_device_uninit(&device_);
    deviceReady_ = false;
  }
}

void AudioCapture::NotificationCallback(const ma_device_notification* notification) {
  if (!notification || !notification->pDevice) return;
  auto* self = static_cast<AudioCapture*>(notification->pDevice->pUserData);
  if (!self) return;
  if (notification->type == ma_device_notification_type_stopped ||
      notification->type == ma_device_notification_type_interruption_began) {
    self->running_.store(false);
  } else if (notification->type == ma_device_notification_type_started ||
             notification->type == ma_device_notification_type_interruption_ended) {
    self->running_.store(true);
  }
}

void AudioCapture::DataCallback(ma_device* device, void* /*output*/,
                                const void* input, ma_uint32 frameCount) {
  auto* self = static_cast<AudioCapture*>(device->pUserData);
  if (!self || !self->callbackGate_.Enter()) return;
  struct CallbackExit {
    detail::AudioCallbackGate& gate;
    ~CallbackExit() { gate.Exit(); }
  } exit{self->callbackGate_};
  if (!input || !self->running_.load()) return;
  const auto* samples = static_cast<const float*>(input);
  ma_uint32 offset = 0;
  while (offset < frameCount) {
    AudioBlock block;
    block.count = std::min<std::size_t>(block.samples.size(), frameCount - offset);
    std::memcpy(block.samples.data(), samples + offset,
                block.count * sizeof(float));
    (void)self->queue_.TryPush(std::move(block));
    offset += static_cast<ma_uint32>(block.count);
  }
}

}  // namespace prismforge
