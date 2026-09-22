#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <SpoutDX.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

namespace {

constexpr UINT kWidth = 1920;
constexpr UINT kHeight = 1080;
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr char kSenderName[] = "PrismForge";
std::atomic_bool g_running{true};

BOOL WINAPI on_console_event(DWORD event) {
  if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
      event == CTRL_CLOSE_EVENT) {
    g_running = false;
    return TRUE;
  }
  return FALSE;
}

struct Options {
  bool probe = false;
  int seconds = 10;
};

bool parse_options(int argc, char** argv, Options& out) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--probe") {
      out.probe = true;
    } else if (arg == "--seconds" && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out.seconds);
      if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
          out.seconds < 1 || out.seconds > 7200) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

void report_hresult(const char* operation, HRESULT hr) {
  std::cerr << operation << " failed (HRESULT 0x" << std::hex
            << static_cast<unsigned long>(hr) << std::dec << ")\n";
}

bool create_rtx_device(ComPtr<ID3D11Device>& device,
                       ComPtr<ID3D11DeviceContext>& context) {
  ComPtr<IDXGIFactory1> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    report_hresult("CreateDXGIFactory1", hr);
    return false;
  }

  ComPtr<IDXGIAdapter1> rtx_adapter;
  DXGI_ADAPTER_DESC1 selected{};
  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> candidate;
    hr = factory->EnumAdapters1(index, &candidate);
    if (hr == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(hr)) {
      report_hresult("EnumAdapters1", hr);
      return false;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(candidate->GetDesc1(&desc))) continue;
    std::wcout << L"Adapter " << index << L": " << desc.Description << L"\n";
    if (desc.VendorId == 0x10DE &&
        std::wstring_view(desc.Description).find(L"RTX 4070") !=
            std::wstring_view::npos) {
      rtx_adapter = candidate;
      selected = desc;
      break;
    }
  }

  if (!rtx_adapter) {
    std::cerr << "RTX 4070 adapter not found; refusing to test on a different GPU.\n";
    return false;
  }

  constexpr D3D_FEATURE_LEVEL levels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL actual{};
  hr = D3D11CreateDevice(rtx_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                         D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                         static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                         &device, &actual, &context);
  if (FAILED(hr)) {
    report_hresult("D3D11CreateDevice", hr);
    return false;
  }
  std::wcout << L"Selected: " << selected.Description << L" (feature level 0x"
             << std::hex << actual << std::dec << L")\n";
  return true;
}

const char* kShader = R"hlsl(
cbuffer Params : register(b0) { float elapsed; float3 padding; };
struct VertexOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VertexOut vertex_main(uint id : SV_VertexID) {
  float2 corner = float2((id << 1) & 2, id & 2);
  VertexOut output;
  output.position = float4(corner.x * 2 - 1, 1 - corner.y * 2, 0, 1);
  output.uv = corner;
  return output;
}
float4 pixel_main(VertexOut input) : SV_Target {
  float2 uv = input.uv;
  float sweep = 0.5 + 0.5 * sin(elapsed * 2.0 - uv.x * 14.0);
  float grid = step(0.93, frac(uv.x * 12)) + step(0.93, frac(uv.y * 7));
  float3 color = float3(0.08 + 0.85 * uv.x,
                        0.06 + 0.75 * sweep,
                        0.18 + 0.70 * uv.y);
  return float4(saturate(color + min(grid, 1.0) * 0.22), 1.0);
}
)hlsl";

bool compile_shader(const char* entry, const char* profile, ComPtr<ID3DBlob>& blob) {
  ComPtr<ID3DBlob> errors;
  const HRESULT hr = D3DCompile(kShader, std::char_traits<char>::length(kShader),
                                "PrismForgeSpike.hlsl", nullptr, nullptr, entry,
                                profile, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                &blob, &errors);
  if (FAILED(hr)) {
    report_hresult("D3DCompile", hr);
    if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
                                static_cast<std::streamsize>(errors->GetBufferSize()));
    return false;
  }
  return true;
}

int run_sender(ID3D11Device* device, ID3D11DeviceContext* context, int seconds) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = kWidth;
  desc.Height = kHeight;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = kFormat;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
  if (FAILED(hr)) { report_hresult("CreateTexture2D", hr); return 1; }
  ComPtr<ID3D11RenderTargetView> target;
  hr = device->CreateRenderTargetView(texture.Get(), nullptr, &target);
  if (FAILED(hr)) { report_hresult("CreateRenderTargetView", hr); return 1; }

  ComPtr<ID3DBlob> vs_blob, ps_blob;
  if (!compile_shader("vertex_main", "vs_5_0", vs_blob) ||
      !compile_shader("pixel_main", "ps_5_0", ps_blob)) return 1;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
                                  nullptr, &vs);
  if (FAILED(hr)) { report_hresult("CreateVertexShader", hr); return 1; }
  hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(),
                                 nullptr, &ps);
  if (FAILED(hr)) { report_hresult("CreatePixelShader", hr); return 1; }

  D3D11_BUFFER_DESC constant_desc{};
  constant_desc.ByteWidth = 16;
  constant_desc.Usage = D3D11_USAGE_DEFAULT;
  constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  ComPtr<ID3D11Buffer> constants;
  hr = device->CreateBuffer(&constant_desc, nullptr, &constants);
  if (FAILED(hr)) { report_hresult("CreateBuffer", hr); return 1; }

  spoutDX sender;
  if (!sender.OpenDirectX11(device) || !sender.SetSenderName(kSenderName)) {
    std::cerr << "Spout sender initialization failed.\n";
    return 1;
  }

  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(kWidth);
  viewport.Height = static_cast<float>(kHeight);
  viewport.MaxDepth = 1.0f;
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vs.Get(), nullptr, 0);
  context->PSSetShader(ps.Get(), nullptr, 0);
  context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());

  std::cout << "Sender: " << kSenderName << ", " << kWidth << "x" << kHeight
            << ", BGRA8, target 60 fps, duration " << seconds << " s\n";
  const auto start = Clock::now();
  auto next_frame = start;
  auto last_report = start;
  std::uint64_t frames = 0;
  while (g_running && Clock::now() - start < std::chrono::seconds(seconds)) {
    const float elapsed = std::chrono::duration<float>(Clock::now() - start).count();
    const float shader_time[4] = {elapsed, 0, 0, 0};
    context->UpdateSubresource(constants.Get(), 0, nullptr, shader_time, 0, 0);
    context->Draw(3, 0);
    if (!sender.SendTexture(texture.Get())) {
      std::cerr << "Spout SendTexture failed at frame " << frames << ".\n";
      return 2;
    }
    ++frames;
    const auto now = Clock::now();
    if (now - last_report >= std::chrono::seconds(10)) {
      const double fps = frames / std::chrono::duration<double>(now - start).count();
      std::cout << "frames=" << frames << " average_fps=" << std::fixed
                << std::setprecision(2) << fps << " spout_fps=" << sender.GetFps()
                << "\n";
      last_report = now;
    }
    next_frame += std::chrono::nanoseconds(16'666'667);
    std::this_thread::sleep_until(next_frame);
    if (next_frame < Clock::now() - std::chrono::milliseconds(100)) {
      next_frame = Clock::now();
    }
  }
  const double fps = frames / std::chrono::duration<double>(Clock::now() - start).count();
  std::cout << "Complete: frames=" << frames << " average_fps=" << std::fixed
            << std::setprecision(2) << fps << "\n";
  sender.ReleaseSender();
  sender.CloseDirectX11();
  return 0;
}

int run_probe(ID3D11Device* device, ID3D11DeviceContext* context, int seconds) {
  spoutDX receiver;
  receiver.SetReceiverName(kSenderName);
  if (!receiver.OpenDirectX11(device)) {
    std::cerr << "Spout receiver initialization failed.\n";
    return 1;
  }
  std::uint64_t received = 0;
  std::uint64_t changed_samples = 0;
  std::optional<std::uint32_t> first_color;
  ComPtr<ID3D11Texture2D> pixel_readback;
  D3D11_TEXTURE2D_DESC readback_desc{};
  readback_desc.Width = 1;
  readback_desc.Height = 1;
  readback_desc.MipLevels = 1;
  readback_desc.ArraySize = 1;
  readback_desc.Format = kFormat;
  readback_desc.SampleDesc.Count = 1;
  readback_desc.Usage = D3D11_USAGE_STAGING;
  readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  const HRESULT create_hr = device->CreateTexture2D(&readback_desc, nullptr, &pixel_readback);
  if (FAILED(create_hr)) { report_hresult("CreateTexture2D readback", create_hr); return 1; }
  const auto start = Clock::now();
  auto next_sample = start;
  while (g_running && Clock::now() - start < std::chrono::seconds(seconds)) {
    if (receiver.ReceiveTexture() && receiver.IsConnected() &&
        receiver.GetSenderTexture()) {
      if (receiver.GetSenderWidth() != kWidth ||
          receiver.GetSenderHeight() != kHeight ||
          receiver.GetSenderFormat() != kFormat) {
        std::cerr << "Sender dimensions or format changed unexpectedly.\n";
        return 2;
      }
      if (receiver.IsFrameNew()) {
        ++received;
        if (Clock::now() >= next_sample) {
          const D3D11_BOX box = {kWidth / 2, kHeight / 2, 0,
                                 kWidth / 2 + 1, kHeight / 2 + 1, 1};
          context->CopySubresourceRegion(pixel_readback.Get(), 0, 0, 0, 0,
                                         receiver.GetSenderTexture(), 0, &box);
          D3D11_MAPPED_SUBRESOURCE mapped{};
          const HRESULT map_hr = context->Map(pixel_readback.Get(), 0, D3D11_MAP_READ,
                                               0, &mapped);
          if (FAILED(map_hr)) { report_hresult("Map readback", map_hr); return 2; }
          std::uint32_t color = 0;
          std::memcpy(&color, mapped.pData, sizeof(color));
          context->Unmap(pixel_readback.Get(), 0);
          if (!first_color) first_color = color;
          else if (color != *first_color) ++changed_samples;
          next_sample += std::chrono::milliseconds(200);
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }
  std::cout << "Probe: connected=" << std::boolalpha << receiver.IsConnected()
            << " new_frames=" << received << " changed_pixel_samples="
            << changed_samples << " expected=" << kWidth << "x"
            << kHeight << " BGRA8\n";
  receiver.ReleaseReceiver();
  receiver.CloseDirectX11();
  return received >= 2 && changed_samples >= 2 ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    std::cerr << "Usage: PrismForge.SpoutSpike [--seconds 1..7200] [--probe]\n";
    return 64;
  }
  SetConsoleCtrlHandler(on_console_event, TRUE);
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (!create_rtx_device(device, context)) return 1;
  return options.probe ? run_probe(device.Get(), context.Get(), options.seconds)
                       : run_sender(device.Get(), context.Get(), options.seconds);
}
