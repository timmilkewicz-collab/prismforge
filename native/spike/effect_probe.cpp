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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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
  if (result.copiedFiles.size() != 12) {
    error = "Expected exactly 12 scene shaders for the hot-reload probe";
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
  for (int retry = 0; retry < 200; ++retry) {
    if (receiver.ReceiveTexture() && receiver.IsConnected() &&
        receiver.IsFrameNew() && receiver.GetSenderTexture()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool hash_received_frame(spoutDX& receiver, ID3D11Device* device,
                         ID3D11DeviceContext* context, std::uint64_t& hash,
                         double* average_luma, std::uint8_t* maximum_rgb) {
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
  if (average_luma || maximum_rgb) {
    std::uint64_t total = 0;
    std::uint8_t maximum = 0;
    for (UINT y = 0; y < desc.Height; ++y) {
      const auto* scanline = bytes + y * mapped.RowPitch;
      for (UINT x = 0; x < desc.Width; ++x) {
        const auto* pixel = scanline + x * 4;
        total += pixel[0] + pixel[1] + pixel[2];
        for (unsigned channel = 0; channel < 3; ++channel) {
          if (pixel[channel] > maximum) maximum = pixel[channel];
        }
      }
    }
    if (average_luma) {
      *average_luma = static_cast<double>(total) /
                      (3.0 * 255.0 * desc.Width * desc.Height);
    }
    if (maximum_rgb) *maximum_rgb = maximum;
  }
  context->Unmap(readback.Get(), 0);
  return true;
}

bool render_and_hash(SpoutRender& renderer, spoutDX& receiver,
                     ID3D11Device* device, ID3D11DeviceContext* context,
                     const ShowSnapshot& show, double seconds,
                     std::uint64_t& hash, std::string& error,
                     const QualityTier& quality = QualityTier{1.0f, 60},
                     double* average_luma = nullptr,
                     std::uint8_t* maximum_rgb = nullptr) {
  SignalFrameV1 signal{};
  signal.bass = 0.4f;
  signal.mids = 0.3f;
  signal.highs = 0.2f;
  if (!renderer.Render(show, signal, quality, seconds, error))
    return false;
  if (!receive_new_frame(receiver)) {
    error = "Receiver did not observe a new Spout frame";
    return false;
  }
  if (!hash_received_frame(receiver, device, context, hash,
                           average_luma, maximum_rgb)) {
    error = "Could not read received Spout pixels";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: PrismForge.EffectProbe <assets/shaders directory>\n";
    return 64;
  }
  SpoutRender renderer;
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
  receiver.SetReceiverName("PrismForge");
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
  }
  show.decks[0].effects = {};
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
  receiver.ReleaseReceiver();
  receiver.CloseDirectX11();
  renderer.Shutdown();
  return unchanged == 0 ? 0 : 2;
}
