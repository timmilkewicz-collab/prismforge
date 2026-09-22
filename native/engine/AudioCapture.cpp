#define MA_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "AudioCapture.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace prismforge {

AudioCapture::AudioCapture(BoundedQueue<AudioBlock>& queue) : queue_(queue) {}
AudioCapture::~AudioCapture() { Stop(); }

std::vector<AudioDeviceInfo> AudioCapture::Devices(std::string& error) {
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
    result.push_back({"loopback:" + std::to_string(i), playback[i].name, true});
  }
  for (ma_uint32 i = 0; i < captureCount; ++i) {
    result.push_back({"input:" + std::to_string(i), capture[i].name, false});
  }
  return result;
}

bool AudioCapture::Start(bool loopback, const std::string& deviceName,
                         std::string& error) {
  Stop();
  (void)Devices(error);
  if (!contextReady_) return false;
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
  config.pUserData = this;
  if (!deviceName.empty()) {
    const auto* devices = loopback ? playback : capture;
    const ma_uint32 count = loopback ? playbackCount : captureCount;
    const ma_device_info* selected = nullptr;
    for (ma_uint32 i = 0; i < count; ++i) {
      const std::string id = std::string(loopback ? "loopback:" : "input:") +
          std::to_string(i);
      if (deviceName == devices[i].name || deviceName == id) {
        selected = &devices[i];
        break;
      }
    }
    if (!selected) {
      error = "Requested audio device was not found: " + deviceName;
      return false;
    }
    config.capture.pDeviceID = &selected->id;
  }

  ma_result status = ma_device_init(&context_, &config, &device_);
  if (status != MA_SUCCESS) {
    error = std::string("Audio device initialization: ") + ma_result_description(status);
    return false;
  }
  deviceReady_ = true;
  sampleRate_ = device_.sampleRate;
  status = ma_device_start(&device_);
  if (status != MA_SUCCESS) {
    error = std::string("Audio device start: ") + ma_result_description(status);
    Stop();
    return false;
  }
  running_ = true;
  return true;
}

void AudioCapture::Stop() {
  if (deviceReady_) {
    ma_device_uninit(&device_);
    deviceReady_ = false;
  }
  running_ = false;
}

void AudioCapture::DataCallback(ma_device* device, void* /*output*/,
                                const void* input, ma_uint32 frameCount) {
  auto* self = static_cast<AudioCapture*>(device->pUserData);
  if (!self || !input) return;
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
