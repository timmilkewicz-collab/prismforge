#include "SpoutRender.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <SpoutDX.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace prismforge;

namespace {

struct TemporaryShaderDirectory {
  std::filesystem::path directory;
  std::vector<std::filesystem::path> copiedFiles;

  ~TemporaryShaderDirectory() {
    std::error_code ignored;
    for (const auto& file : copiedFiles) std::filesystem::remove(file, ignored);
    if (!directory.empty()) std::filesystem::remove(directory, ignored);
  }
};

bool copy_shaders_for_probe(const std::filesystem::path& source,
                            TemporaryShaderDirectory& result,
                            std::string& error) {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path() /
      ("PrismForge-EffectProbe-" + std::to_string(GetCurrentProcessId()) +
       "-" + std::to_string(nonce));
  std::error_code ec;
  if (!std::filesystem::create_directory(directory, ec)) {
    error = "Cannot create isolated shader directory: " + ec.message();
    return false;
  }
  result.directory = directory;
  for (const auto& entry : std::filesystem::directory_iterator(source)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".hlsl") continue;
    const auto copy = directory / entry.path().filename();
    if (!std::filesystem::copy_file(entry.path(), copy, ec)) {
      error = "Cannot copy shader for hot-reload probe: " + ec.message();
      return false;
    }
    result.copiedFiles.push_back(copy);
  }
  if (result.copiedFiles.size() != kSceneIds.size()) {
    error = "Expected exactly " + std::to_string(kSceneIds.size()) +
            " scene shaders for the hot-reload probe";
    return false;
  }
  return true;
}

bool create_receiver_device(ComPtr<ID3D11Device>& device) {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> adapter;
    const HRESULT hr = factory->EnumAdapters1(index, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(hr)) return false;
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)) || desc.VendorId != 0x10DE ||
        std::wstring_view(desc.Description).find(L"RTX 4070") ==
            std::wstring_view::npos) continue;
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                             D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL actual{};
    return SUCCEEDED(D3D11CreateDevice(
        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
        &device, &actual, nullptr));
  }
  return false;
}

bool receive_new_frame(spoutDX& receiver) {
  for (int retry = 0; retry < 500; ++retry) {
    const bool received = receiver.ReceiveTexture();
    // Spout pauses copies while its sender-update flag is set; clear that
    // flag before trusting IsFrameNew or the previous texture can look new.
    if (receiver.IsUpdated()) continue;
    if (received && receiver.IsConnected() &&
        receiver.IsFrameNew() && receiver.GetSenderTexture()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool hash_received_frame(spoutDX& receiver, ID3D11Device* device,
                         ID3D11DeviceContext* context, std::uint64_t& hash,
                         double* average_luma, std::uint8_t* maximum_rgb,
                         std::uint64_t* full_frame_hash,
                         std::array<std::uint32_t, 18 * 32>* grid_pixels) {
  auto* source = receiver.GetSenderTexture();
  if (!source || receiver.GetSenderWidth() != SpoutRender::kOutputWidth ||
      receiver.GetSenderHeight() != SpoutRender::kOutputHeight ||
      receiver.GetSenderFormat() != DXGI_FORMAT_B8G8R8A8_UNORM) return false;
  D3D11_TEXTURE2D_DESC desc{};
  source->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> readback;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &readback))) return false;
  context->CopyResource(readback.Get(), source);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
  hash = 1469598103934665603ULL;
  const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData);
  for (unsigned row = 0; row < 9; ++row) {
    const auto y = (row * desc.Height + desc.Height / 2) / 9;
    for (unsigned column = 0; column < 16; ++column) {
      const auto x = (column * desc.Width + desc.Width / 2) / 16;
      std::uint32_t pixel = 0;
      std::memcpy(&pixel, bytes + y * mapped.RowPitch + x * 4, sizeof(pixel));
      hash ^= pixel;
      hash *= 1099511628211ULL;
    }
  }
  if (grid_pixels) {
    for (unsigned row = 0; row < 18; ++row) {
      const auto y = (row * desc.Height + desc.Height / 2) / 18;
      for (unsigned column = 0; column < 32; ++column) {
        const auto x = (column * desc.Width + desc.Width / 2) / 32;
        std::memcpy(&(*grid_pixels)[row * 32 + column],
                    bytes + y * mapped.RowPitch + x * 4,
                    sizeof(std::uint32_t));
      }
    }
  }
  if (average_luma || maximum_rgb || full_frame_hash) {
    std::uint64_t total = 0;
    std::uint8_t maximum = 0;
    std::uint64_t full_hash = 1469598103934665603ULL;
    for (UINT y = 0; y < desc.Height; ++y) {
      const auto* scanline = bytes + y * mapped.RowPitch;
      for (UINT x = 0; x < desc.Width; ++x) {
        const auto* pixel = scanline + x * 4;
        if (average_luma) total += pixel[0] + pixel[1] + pixel[2];
        for (unsigned channel = 0; channel < 3; ++channel) {
          if (maximum_rgb && pixel[channel] > maximum)
            maximum = pixel[channel];
          if (full_frame_hash) {
            full_hash ^= pixel[channel];
            full_hash *= 1099511628211ULL;
          }
        }
      }
    }
    if (average_luma) {
      *average_luma = static_cast<double>(total) /
                      (3.0 * 255.0 * desc.Width * desc.Height);
    }
    if (maximum_rgb) *maximum_rgb = maximum;
    if (full_frame_hash) *full_frame_hash = full_hash;
  }
  context->Unmap(readback.Get(), 0);
  return true;
}

bool capture_received_bmp(spoutDX& receiver, ID3D11Device* device,
                          ID3D11DeviceContext* context,
                          const std::filesystem::path& path) {
  auto* source = receiver.GetSenderTexture();
  if (!source || receiver.GetSenderWidth() != SpoutRender::kOutputWidth ||
      receiver.GetSenderHeight() != SpoutRender::kOutputHeight ||
      receiver.GetSenderFormat() != DXGI_FORMAT_B8G8R8A8_UNORM) return false;
  D3D11_TEXTURE2D_DESC desc{};
  source->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> readback;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &readback))) return false;
  context->CopyResource(readback.Get(), source);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    return false;
  BITMAPINFOHEADER info{};
  info.biSize = sizeof(info);
  info.biWidth = static_cast<LONG>(desc.Width);
  info.biHeight = static_cast<LONG>(desc.Height);
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;
  info.biSizeImage = desc.Width * desc.Height * 4;
  BITMAPFILEHEADER file_header{};
  file_header.bfType = 0x4d42;
  file_header.bfOffBits = sizeof(file_header) + sizeof(info);
  file_header.bfSize = file_header.bfOffBits + info.biSizeImage;
  std::ofstream file(path, std::ios::binary);
  if (file) {
    file.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    file.write(reinterpret_cast<const char*>(&info), sizeof(info));
    const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData);
    for (UINT row = desc.Height; row-- > 0;) {
      file.write(reinterpret_cast<const char*>(bytes + row * mapped.RowPitch),
                 desc.Width * 4);
    }
  }
  context->Unmap(readback.Get(), 0);
  return file.good();
}

bool render_and_hash(SpoutRender& renderer, spoutDX& receiver,
                     ID3D11Device* device, ID3D11DeviceContext* context,
                     const ShowSnapshot& show, double seconds,
                     std::uint64_t& hash, std::string& error,
                     const QualityTier& quality = QualityTier{1.0f, 60},
                     double* average_luma = nullptr,
                     std::uint8_t* maximum_rgb = nullptr,
                     const SignalFrameV1* signal_override = nullptr,
                     std::uint64_t* full_frame_hash = nullptr,
                     std::array<std::uint32_t, 18 * 32>* grid_pixels = nullptr) {
  SignalFrameV1 signal{};
  signal.bass = 0.4f;
  signal.mids = 0.3f;
  signal.highs = 0.2f;
  if (signal_override) signal = *signal_override;
  // Shared D3D readback can lag several Spout frame counters on this driver,
  // especially immediately after resizing. Settle a same-time frame train
  // before sampling; the 30 fps gate is checked separately below.
  const unsigned frames = quality.targetFps > 30 ? 16u : 1u;
  for (unsigned frame = 0; frame < frames; ++frame) {
    if (!renderer.Render(show, signal, quality, seconds, error)) return false;
    // Pace the receiver like a live 60 Hz consumer; a tight CPU loop can run
    // far ahead of queued sender GPU work and sample the prior effect/scene.
    if (quality.targetFps > 30)
      std::this_thread::sleep_for(std::chrono::milliseconds(17));
    if (!receive_new_frame(receiver)) {
      error = "Receiver did not observe a new Spout frame";
      return false;
    }
  }
  if (!hash_received_frame(receiver, device, context, hash,
                           average_luma, maximum_rgb, full_frame_hash,
                           grid_pixels)) {
    error = "Could not read received Spout pixels";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && (argc != 4 || std::string_view(argv[2]) != "--capture-dir")) {
    std::cerr << "Usage: PrismForge.EffectProbe <assets/shaders directory> "
                 "[--capture-dir <existing directory>]\n";
    return 64;
  }
  const std::filesystem::path capture_directory =
      argc == 4 ? std::filesystem::path(argv[3]) : std::filesystem::path{};
  if (!capture_directory.empty() &&
      !std::filesystem::is_directory(capture_directory)) {
    std::cerr << "Capture directory does not exist\n";
    return 64;
  }
  SpoutRender renderer("PrismForge.EffectProbe");
  std::string error;
  const std::filesystem::path source_shaders(argv[1]);
  TemporaryShaderDirectory copied_shaders;
  if (!copy_shaders_for_probe(source_shaders, copied_shaders, error)) {
    std::cerr << error << '\n';
    return 1;
  }
  if (!renderer.Initialize(copied_shaders.directory, error)) {
    std::cerr << "Renderer initialization failed: " << error << '\n';
    return 1;
  }
  ComPtr<ID3D11Device> receiver_device;
  if (!create_receiver_device(receiver_device)) {
    std::cerr << "Could not create RTX 4070 receiver device\n";
    return 1;
  }
  ComPtr<ID3D11DeviceContext> receiver_context;
  receiver_device->GetImmediateContext(&receiver_context);
  spoutDX receiver;
  receiver.SetReceiverName("PrismForge.EffectProbe");
  if (!receiver.OpenDirectX11(receiver_device.Get())) {
    std::cerr << "Could not open Spout receiver\n";
    return 1;
  }

  ShowSnapshot show;
  show.decks[0].sceneId = "prism-atrium";
  show.crossfader = 0.0f;
  constexpr double kTime = 1.234;
  std::uint64_t baseline = 0;
  if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                       receiver_context.Get(), show, kTime, baseline, error)) {
    std::cerr << error << '\n';
    return 1;
  }
  std::cout << "baseline=" << baseline << '\n';
  constexpr std::array<const char*, 4> names = {
      "Bloom", "Feedback", "Kaleidoscope", "Pixelate"};
  unsigned unchanged = 0;
  std::uint64_t last_effect_hash = baseline;
  for (unsigned index = 0; index < names.size(); ++index) {
    show.decks[0].effects = {};
    if (index == 1) {
      std::uint64_t seed = 0;
      if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                           receiver_context.Get(), show, 0.123, seed, error)) {
        std::cerr << error << '\n';
        return 1;
      }
    }
    show.decks[0].effects[index] = 1.0f;
    std::uint64_t changed = 0;
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, kTime, changed, error)) {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout << names[index] << '=' << changed
              << " differs=" << (changed != baseline) << '\n';
    if (changed == baseline) ++unchanged;
    last_effect_hash = changed;
  }
  show.decks[0].effects = {};
  struct SceneExpectation {
    std::string_view id;
    double minimumLuma;
    std::uint8_t minimumPeak;
  };
  constexpr std::array<SceneExpectation, 6> new_scenes = {{{
      "hex-vortex", 0.07, 80},
      {"ferrofluid-reactor", 0.07, 80},
      {"shardwell", 0.01, 70},
      {"neon-orbs", 0.003, 70},
      {"mirror-cathedral", 0.003, 70},
      {"recursive-circuit", 0.003, 70},
  }};
  std::array<std::uint64_t, new_scenes.size()> scene_hashes{};
  std::uint64_t previous_scene_hash = last_effect_hash;
  for (unsigned index = 0; index < new_scenes.size(); ++index) {
    show.decks[0].sceneId = std::string(new_scenes[index].id);
    double scene_luma = 0.0;
    std::uint8_t scene_maximum = 0;
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, 2.0,
                         scene_hashes[index], error, {1.0f, 60},
                         &scene_luma, &scene_maximum)) {
      std::cerr << error << '\n';
      return 3;
    }
    std::cout << new_scenes[index].id << " hash=" << scene_hashes[index]
              << " luma=" << scene_luma
              << " max_rgb=" << static_cast<unsigned>(scene_maximum) << '\n';
    if (scene_luma < new_scenes[index].minimumLuma ||
        scene_maximum < new_scenes[index].minimumPeak ||
        scene_hashes[index] == previous_scene_hash) {
      std::cerr << "New scene did not produce a full, distinct received image\n";
      return 3;
    }
    previous_scene_hash = scene_hashes[index];
  }

  // Grid samples can miss a small hit-lit joint, so the focused hero-scene
  // check fingerprints every received RGB pixel. Hold shader time fixed and
  // repeat the baseline first to rule out stale Spout copies or history.
  show.decks[0].sceneId = "mirror-cathedral";
  show.decks[0].sceneParams = {0.5f, 0.5f, 0.5f, 0.5f};
  SignalFrameV1 mirror_signal{};
  mirror_signal.bass = 0.17f;
  mirror_signal.mids = 0.21f;
  mirror_signal.highs = 0.19f;
  auto mirror_fingerprint = [&](const ShowSnapshot& scene,
                                const SignalFrameV1& signal,
                                std::uint64_t& fingerprint) {
    std::uint64_t grid_hash = 0;
    return render_and_hash(renderer, receiver, receiver_device.Get(),
                           receiver_context.Get(), scene, 2.625, grid_hash,
                           error, {1.0f, 60}, nullptr, nullptr, &signal,
                           &fingerprint);
  };
  std::uint64_t mirror_baseline = 0;
  std::uint64_t mirror_repeat = 0;
  if (!mirror_fingerprint(show, mirror_signal, mirror_baseline) ||
      !mirror_fingerprint(show, mirror_signal, mirror_repeat)) {
    std::cerr << error << '\n';
    return 3;
  }
  if (mirror_baseline != mirror_repeat) {
    std::cerr << "Mirror Cathedral fixed-input frame was not stable\n";
    return 3;
  }
  constexpr std::array<const char*, 4> control_names = {
      "symmetry", "depth", "aperture", "line_width"};
  for (unsigned index = 0; index < control_names.size(); ++index) {
    ShowSnapshot variant = show;
    variant.decks[0].sceneParams[index] = 0.91f;
    std::uint64_t fingerprint = 0;
    if (!mirror_fingerprint(variant, mirror_signal, fingerprint)) {
      std::cerr << error << '\n';
      return 3;
    }
    const bool differs = fingerprint != mirror_baseline;
    std::cout << "mirror_control_" << control_names[index]
              << "_differs=" << differs << '\n';
    if (!differs) return 3;
  }
  constexpr std::array<const char*, 4> audio_names = {
      "bass", "mids", "highs", "hit"};
  for (unsigned index = 0; index < audio_names.size(); ++index) {
    SignalFrameV1 variant = mirror_signal;
    if (index == 0) variant.bass = 0.91f;
    if (index == 1) variant.mids = 0.91f;
    if (index == 2) variant.highs = 0.91f;
    if (index == 3) variant.hit = true;
    std::uint64_t fingerprint = 0;
    if (!mirror_fingerprint(show, variant, fingerprint)) {
      std::cerr << error << '\n';
      return 3;
    }
    const bool differs = fingerprint != mirror_baseline;
    std::cout << "mirror_audio_" << audio_names[index]
              << "_differs=" << differs << '\n';
    if (!differs) return 3;
  }

  // Synthetic .91 band inputs are much hotter than the room-mic signal.
  // Sample ordinary music-range values and require visible sampled geometry
  // changes while keeping the average frame luminance bounded.
  struct ReactiveExpectation {
    std::string_view id;
    unsigned minimumChangedSamples;
  };
  constexpr std::array<ReactiveExpectation, 3> reactive_scenes = {{{
      "neon-orbs", 4}, {"shardwell", 8}, {"recursive-circuit", 8},
  }};
  SignalFrameV1 quiet_signal{};
  quiet_signal.bass = 0.004f;
  quiet_signal.mids = 0.004f;
  quiet_signal.highs = 0.004f;
  SignalFrameV1 music_signal{};
  music_signal.bass = 0.06f;
  music_signal.mids = 0.08f;
  music_signal.highs = 0.05f;
  for (const auto& expectation : reactive_scenes) {
    show.decks[0].sceneId = std::string(expectation.id);
    show.decks[0].sceneParams = {0.5f, 0.5f, 0.5f, 0.5f};
    std::array<std::uint32_t, 18 * 32> quiet_pixels{};
    std::array<std::uint32_t, 18 * 32> music_pixels{};
    std::uint64_t quiet_hash = 0;
    std::uint64_t music_hash = 0;
    double quiet_luma = 0.0;
    double music_luma = 0.0;
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, 3.125, quiet_hash,
                         error, {1.0f, 60}, &quiet_luma, nullptr,
                         &quiet_signal, nullptr, &quiet_pixels)) {
      std::cerr << error << '\n';
      return 3;
    }
    if (!capture_directory.empty() &&
        !capture_received_bmp(receiver, receiver_device.Get(),
                              receiver_context.Get(),
                              capture_directory /
                                  (std::string(expectation.id) + "-quiet.bmp"))) {
      std::cerr << "Could not capture quiet scene frame\n";
      return 3;
    }
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, 3.125, music_hash,
                         error, {1.0f, 60}, &music_luma, nullptr,
                         &music_signal, nullptr, &music_pixels)) {
      std::cerr << error << '\n';
      return 3;
    }
    if (!capture_directory.empty() &&
        !capture_received_bmp(receiver, receiver_device.Get(),
                              receiver_context.Get(),
                              capture_directory /
                                  (std::string(expectation.id) + "-music.bmp"))) {
      std::cerr << "Could not capture music-range scene frame\n";
      return 3;
    }
    unsigned changed_samples = 0;
    for (unsigned index = 0; index < quiet_pixels.size(); ++index) {
      const auto quiet = quiet_pixels[index];
      const auto music = music_pixels[index];
      unsigned channel_delta = 0;
      for (unsigned channel = 0; channel < 3; ++channel) {
        const unsigned shift = channel * 8;
        channel_delta += static_cast<unsigned>(std::abs(
            static_cast<int>((quiet >> shift) & 0xffu) -
            static_cast<int>((music >> shift) & 0xffu)));
      }
      if (channel_delta >= 12) ++changed_samples;
    }
    std::cout << expectation.id << " music_range_changed_samples="
              << changed_samples << " quiet_luma=" << quiet_luma
              << " music_luma=" << music_luma << '\n';
    if (quiet_hash == music_hash ||
        changed_samples < expectation.minimumChangedSamples ||
        std::abs(music_luma - quiet_luma) > 0.08) {
      std::cerr << "Mic-range scene reaction was absent or too global\n";
      return 3;
    }
  }
  show.decks[0].sceneId = "recursive-circuit";
  show.decks[0].sceneParams = {0.5f, 0.5f, 0.5f, 0.5f};
  SignalFrameV1 circuit_signal{};
  circuit_signal.bass = 0.03f;
  circuit_signal.mids = 0.03f;
  circuit_signal.highs = 0.03f;
  auto circuit_fingerprint = [&](const ShowSnapshot& scene,
                                 const SignalFrameV1& signal,
                                 std::uint64_t& fingerprint) {
    std::uint64_t grid_hash = 0;
    return render_and_hash(renderer, receiver, receiver_device.Get(),
                           receiver_context.Get(), scene, 4.0, grid_hash,
                           error, {1.0f, 60}, nullptr, nullptr, &signal,
                           &fingerprint);
  };
  std::uint64_t circuit_baseline = 0;
  if (!circuit_fingerprint(show, circuit_signal, circuit_baseline)) {
    std::cerr << error << '\n';
    return 3;
  }
  constexpr std::array<const char*, 4> circuit_control_names = {
      "branching", "flow", "depth", "charge"};
  for (unsigned index = 0; index < circuit_control_names.size(); ++index) {
    ShowSnapshot variant = show;
    variant.decks[0].sceneParams[index] = 0.91f;
    std::uint64_t fingerprint = 0;
    if (!circuit_fingerprint(variant, circuit_signal, fingerprint)) {
      std::cerr << error << '\n';
      return 3;
    }
    const bool differs = fingerprint != circuit_baseline;
    std::cout << "circuit_control_" << circuit_control_names[index]
              << "_differs=" << differs << '\n';
    if (!differs) return 3;
  }
  for (unsigned index = 0; index < audio_names.size(); ++index) {
    SignalFrameV1 variant = circuit_signal;
    if (index == 0) variant.bass = 0.08f;
    if (index == 1) variant.mids = 0.08f;
    if (index == 2) variant.highs = 0.08f;
    if (index == 3) variant.hit = true;
    std::uint64_t fingerprint = 0;
    if (!circuit_fingerprint(show, variant, fingerprint)) {
      std::cerr << error << '\n';
      return 3;
    }
    const bool differs = fingerprint != circuit_baseline;
    std::cout << "circuit_audio_" << audio_names[index]
              << "_differs=" << differs << '\n';
    if (!differs) return 3;
  }
  show.decks[0].sceneId = "prism-atrium";

  constexpr std::array<QualityTier, 4> tiers = {{
      {1.0f, 60}, {0.75f, 60}, {0.66f, 60}, {2.0f / 3.0f, 30}}};
  for (unsigned index = 0; index < tiers.size(); ++index) {
    std::uint64_t frame_hash = 0;
    const double time = 10.0 + index;
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, time, frame_hash,
                         error, tiers[index])) {
      std::cerr << error << '\n';
      return 1;
    }
    const auto expected_width = static_cast<unsigned>(
        std::lround(SpoutRender::kOutputWidth * tiers[index].internalScale));
    const auto expected_height = static_cast<unsigned>(
        std::lround(SpoutRender::kOutputHeight * tiers[index].internalScale));
    const bool correct = renderer.InternalWidth() == expected_width &&
                         renderer.InternalHeight() == expected_height;
    std::cout << "tier=" << index << " internal=" << renderer.InternalWidth()
              << 'x' << renderer.InternalHeight() << " output=1920x1080 BGRA8"
              << " correct=" << correct << '\n';
    if (!correct) return 3;
  }
  SignalFrameV1 silent{};
  if (!renderer.Render(show, silent, tiers.back(), 13.01, error)) {
    std::cerr << error << '\n';
    return 1;
  }
  receiver.ReceiveTexture();
  if (receiver.IsFrameNew()) {
    std::cerr << "Safety tier published before its 30 fps interval\n";
    return 3;
  }
  if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                       receiver_context.Get(), show, 13.04, baseline,
                       error, tiers.back())) {
    std::cerr << error << '\n';
    return 3;
  }
  std::cout << "safety_tier_30fps_gate=true\n";
  show.blackout = false;
  show.panicDim = false;
  double normal_luma = 0.0;
  if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                       receiver_context.Get(), show, 20.0, baseline,
                       error, tiers.front(), &normal_luma)) {
    std::cerr << error << '\n';
    return 3;
  }
  show.panicDim = true;
  std::uint64_t dim_hash = 0;
  double dim_luma = 0.0;
  if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                       receiver_context.Get(), show, 20.0, dim_hash,
                       error, tiers.front(), &dim_luma)) {
    std::cerr << error << '\n';
    return 3;
  }
  std::cout << "panic_dim_luma=" << dim_luma << " normal_luma="
            << normal_luma << '\n';
  if (!(dim_luma < normal_luma * 0.25)) return 3;
  show.panicDim = false;
  show.blackout = true;
  std::uint64_t black_hash = 0;
  std::uint8_t maximum_rgb = 255;
  if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                       receiver_context.Get(), show, 20.0, black_hash,
                       error, tiers.front(), nullptr, &maximum_rgb)) {
    std::cerr << error << '\n';
    return 3;
  }
  std::cout << "blackout_max_rgb=" << static_cast<unsigned>(maximum_rgb) << '\n';
  if (maximum_rgb != 0) return 3;

  show.blackout = false;
  std::uint64_t pre_reload_hash = 0;
  if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                       receiver_context.Get(), show, 21.0, pre_reload_hash,
                       error, tiers.front())) {
    std::cerr << error << '\n';
    return 3;
  }
  const auto mutated_shader = copied_shaders.directory / "PrismAtrium.hlsl";
  {
    std::ofstream broken(mutated_shader, std::ios::trunc);
    broken << "this is deliberately invalid HLSL @\n";
    if (!broken) {
      std::cerr << "Could not write isolated invalid shader\n";
      return 3;
    }
  }
  if (renderer.ReloadShaders(error) || error.empty()) {
    std::cerr << "Invalid shader reload was not rejected\n";
    return 3;
  }
  std::uint64_t post_reload_hash = 0;
  if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                       receiver_context.Get(), show, 21.0, post_reload_hash,
                       error, tiers.front())) {
    std::cerr << error << '\n';
    return 3;
  }
  std::cout << "invalid_reload_rejected=true retained_output="
            << (pre_reload_hash == post_reload_hash) << '\n';
  if (pre_reload_hash != post_reload_hash) return 3;

  std::error_code restore_error;
  std::filesystem::copy_file(source_shaders / "PrismAtrium.hlsl",
                             mutated_shader,
                             std::filesystem::copy_options::overwrite_existing,
                             restore_error);
  if (restore_error || !renderer.ReloadShaders(error)) {
    std::cerr << "Valid shader reload did not recover: "
              << (restore_error ? restore_error.message() : error) << '\n';
    return 3;
  }
  std::cout << "valid_reload_recovered=true\n";

  // Same-time A/B after a forward warmup: the reverse timestamp holds the
  // macro smoothing state while the scene shader receives the baseline time.
  // Test Motion last because its integrated scene clock intentionally persists.
  constexpr std::array<std::pair<unsigned, const char*>, 4> macros = {{
      {1, "Warp"}, {2, "Trails"}, {3, "Color"}, {0, "Motion"}}};
  for (unsigned step = 0; step < macros.size(); ++step) {
    show.masterEffects = {};
    const double baseline_time = 31.0 + step * 10.0;
    for (unsigned settle = 0; settle < 3; ++settle) {
      std::uint64_t ignored = 0;
      if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                           receiver_context.Get(), show,
                           baseline_time - 0.75 + settle * 0.25,
                           ignored, error)) {
        std::cerr << error << '\n';
        return 3;
      }
    }
    std::uint64_t neutral_hash = 0;
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, baseline_time,
                         neutral_hash, error)) {
      std::cerr << error << '\n';
      return 3;
    }
    show.masterEffects[macros[step].first] = 1.0f;
    std::uint64_t warm_hash = 0;
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, baseline_time + 0.25,
                         warm_hash, error)) {
      std::cerr << error << '\n';
      return 3;
    }
    std::uint64_t macro_hash = 0;
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, baseline_time,
                         macro_hash, error)) {
      std::cerr << error << '\n';
      return 3;
    }
    const bool differs = macro_hash != neutral_hash;
    std::cout << "macro_" << macros[step].second << "=" << macro_hash
              << " differs=" << differs << '\n';
    if (!differs) return 3;
  }

  // Full deck Feedback plus master Trails must not replace every current
  // pixel with fading history. Exercise the combination through 60 rendered
  // frames (20 probe calls, three publications each) and retain visible light.
  show.masterEffects = {};
  show.decks[0].effects = {};
  show.decks[0].sceneId = "ferrofluid-reactor";
  double neutral_luma = 0.0;
  std::uint64_t neutral_hash = 0;
  if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                       receiver_context.Get(), show, 70.0, neutral_hash,
                       error, {1.0f, 60}, &neutral_luma)) {
    std::cerr << error << '\n';
    return 3;
  }
  show.decks[0].effects[1] = 1.0f;
  show.masterEffects[2] = 1.0f;
  double combined_luma = 0.0;
  for (unsigned frame = 0; frame < 20; ++frame) {
    std::uint64_t combined_hash = 0;
    if (!render_and_hash(renderer, receiver, receiver_device.Get(),
                         receiver_context.Get(), show, 71.0 + frame / 60.0,
                         combined_hash, error, {1.0f, 60},
                         &combined_luma)) {
      std::cerr << error << '\n';
      return 3;
    }
  }
  std::cout << "feedback_trails_luma=" << combined_luma
            << " neutral_luma=" << neutral_luma << '\n';
  if (!(combined_luma > neutral_luma * 0.10)) return 3;

  receiver.ReleaseReceiver();
  receiver.CloseDirectX11();
  renderer.Shutdown();
  return unchanged == 0 ? 0 : 2;
}
