#include "SpoutRender.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <SpoutDX.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace prismforge {
namespace {

using Microsoft::WRL::ComPtr;
constexpr DXGI_FORMAT kPixelFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr std::string_view kSenderName = "PrismForge";

struct SceneSpec {
  std::string_view id;
  std::string_view file;
};

constexpr std::array<SceneSpec, 12> kScenes = {{
    {"ink-tide", "InkTide.hlsl"},
    {"prism-atrium", "PrismAtrium.hlsl"},
    {"chrome-flock", "ChromeFlock.hlsl"},
    {"signal-lab", "SignalLab.hlsl"},
    {"neon-rift", "NeonRift.hlsl"},
    {"media-alchemy", "MediaAlchemy.hlsl"},
    {"reaction-bloom", "ReactionBloom.hlsl"},
    {"harmonic-sand", "HarmonicSand.hlsl"},
    {"constellation", "Constellation.hlsl"},
    {"fold-temple", "FoldTemple.hlsl"},
    {"dream-grove", "DreamGrove.hlsl"},
    {"living-point-cloud", "LivingPointCloud.hlsl"},
}};

constexpr char kFullscreenShader[] = R"hlsl(
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
PSInput main(uint id : SV_VertexID) {
  float2 corner = float2((id << 1) & 2, id & 2);
  PSInput output;
  output.position = float4(corner.x * 2 - 1, 1 - corner.y * 2, 0, 1);
  output.uv = corner;
  return output;
}
)hlsl";

constexpr char kCompositeShader[] = R"hlsl(
Texture2D deckA : register(t0);
Texture2D deckB : register(t1);
SamplerState linearSampler : register(s0);
cbuffer CompositeInputs : register(b0) {
  float crossfader;
  float gain;
  float2 pad;
};
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
float4 main(PSInput input) : SV_TARGET {
  float3 a = deckA.Sample(linearSampler, input.uv).rgb;
  float3 b = deckB.Sample(linearSampler, input.uv).rgb;
  return float4(saturate(lerp(a, b, saturate(crossfader)) * gain), 1.0);
}
)hlsl";

constexpr char kEffectShader[] = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D historyTexture : register(t1);
SamplerState linearSampler : register(s0);
cbuffer EffectInputs : register(b0) {
  float bloom;
  float feedback;
  float kaleidoscope;
  float pixelate;
  float2 texel;
  float historyReady;
  float pad;
};
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
float2 effect_uv(float2 uv) {
  if (kaleidoscope > 0.001) {
    float aspect = texel.y / max(texel.x, 0.000001);
    float2 p = (uv - 0.5) * float2(aspect, 1.0);
    float angle = atan2(p.y, p.x) + 3.14159265;
    float sector = 6.2831853 / round(lerp(2.0, 10.0, saturate(kaleidoscope)));
    float fold = abs(frac(angle / sector) * 2.0 - 1.0) * sector * 0.5;
    float2 mirrored = 0.5 + length(p) * float2(cos(fold), sin(fold)) /
                         float2(aspect, 1.0);
    uv = lerp(uv, mirrored, saturate(kaleidoscope));
  }
  if (pixelate > 0.001) {
    float2 nativeGrid = 1.0 / max(texel, float2(0.000001, 0.000001));
    float2 grid = lerp(nativeGrid, float2(48.0, 27.0), saturate(pixelate));
    uv = (floor(uv * grid) + 0.5) / grid;
  }
  return saturate(uv);
}
float3 bright(float2 uv) {
  float3 sampleColor = sceneTexture.Sample(linearSampler, saturate(uv)).rgb;
  return sampleColor * 0.45 + max(sampleColor - 0.10, 0.0);
}
float4 main(PSInput input) : SV_TARGET {
  float2 uv = effect_uv(input.uv);
  float3 color = sceneTexture.Sample(linearSampler, uv).rgb;
  if (bloom > 0.001) {
    float2 spread = texel * 14.0;
    float3 glow = bright(uv + float2(spread.x, 0.0)) +
                  bright(uv - float2(spread.x, 0.0)) +
                  bright(uv + float2(0.0, spread.y)) +
                  bright(uv - float2(0.0, spread.y));
    color += glow * (0.34 * saturate(bloom));
  }
  if (feedback > 0.001 && historyReady > 0.5) {
    float3 previous = historyTexture.Sample(linearSampler, input.uv).rgb;
    color = lerp(color, previous, saturate(feedback) * 0.88);
  }
  return float4(saturate(color), 1.0);
}
)hlsl";

// SceneInputs matches the HLSL cbuffer b0 in assets/shaders. HLSL places
// float2 pad in the third 16-byte register because it cannot fit after hit
// and resolution in the second register.
struct alignas(16) SceneInputs {
  float time, bass, mids, highs;
  float hit, width, height, unused0;
  float unused1, unused2, unused3, unused4;
};
static_assert(sizeof(SceneInputs) == 48);

struct alignas(16) CompositeInputs {
  float crossfader, gain, pad0, pad1;
};
static_assert(sizeof(CompositeInputs) == 16);

struct alignas(16) EffectInputs {
  float bloom, feedback, kaleidoscope, pixelate;
  float texelX, texelY, historyReady, pad;
};
static_assert(sizeof(EffectInputs) == 32);

float finite_unit(float value) {
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

std::string hr_text(std::string_view operation, HRESULT hr) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result(operation);
  result += " failed (HRESULT 0x";
  const auto code = static_cast<std::uint32_t>(hr);
  for (int shift = 28; shift >= 0; shift -= 4) {
    result += digits[(code >> shift) & 0xF];
  }
  result += ')';
  return result;
}

bool compile_hlsl(std::string_view source, const std::string& label,
                  const char* entry, const char* profile,
                  ComPtr<ID3DBlob>& bytecode, std::string& error) {
  ComPtr<ID3DBlob> compiler_errors;
  const HRESULT hr = D3DCompile(source.data(), source.size(), label.c_str(),
                                nullptr, nullptr, entry, profile,
                                D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                &bytecode, &compiler_errors);
  if (FAILED(hr)) {
    error = hr_text("Shader " + label, hr);
    if (compiler_errors) {
      error += ": ";
      error.append(static_cast<const char*>(compiler_errors->GetBufferPointer()),
                   compiler_errors->GetBufferSize());
    }
    return false;
  }
  return true;
}

bool select_rtx_4070(ComPtr<IDXGIAdapter1>& adapter, std::string& error) {
  ComPtr<IDXGIFactory1> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) { error = hr_text("CreateDXGIFactory1", hr); return false; }
  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> candidate;
    hr = factory->EnumAdapters1(index, &candidate);
    if (hr == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(hr)) { error = hr_text("EnumAdapters1", hr); return false; }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(candidate->GetDesc1(&desc))) continue;
    if (desc.VendorId == 0x10DE &&
        std::wstring_view(desc.Description).find(L"RTX 4070") !=
            std::wstring_view::npos) {
      adapter = std::move(candidate);
      return true;
    }
  }
  error = "NVIDIA RTX 4070 adapter was not found";
  return false;
}

struct Surface {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> renderTarget;
  ComPtr<ID3D11ShaderResourceView> shaderResource;
};

bool create_surface(ID3D11Device* device, UINT width, UINT height,
                    Surface& surface, std::string& error) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = kPixelFormat;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  Surface next;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, &next.texture);
  if (FAILED(hr)) { error = hr_text("CreateTexture2D", hr); return false; }
  hr = device->CreateRenderTargetView(next.texture.Get(), nullptr,
                                      &next.renderTarget);
  if (FAILED(hr)) { error = hr_text("CreateRenderTargetView", hr); return false; }
  hr = device->CreateShaderResourceView(next.texture.Get(), nullptr,
                                        &next.shaderResource);
  if (FAILED(hr)) { error = hr_text("CreateShaderResourceView", hr); return false; }
  surface = std::move(next);
  return true;
}

unsigned scene_index(std::string_view id) {
  for (unsigned i = 0; i < kScenes.size(); ++i) {
    if (kScenes[i].id == id) return i;
  }
  return 0;  // A known safe scene for IDs whose shader is not yet shipped.
}

}  // namespace

struct SpoutRender::Impl {
  std::filesystem::path shaderDirectory;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11VertexShader> vertexShader;
  ComPtr<ID3D11PixelShader> compositeShader;
  ComPtr<ID3D11PixelShader> effectShader;
  std::array<ComPtr<ID3D11PixelShader>, kScenes.size()> sceneShaders;
  ComPtr<ID3D11Buffer> sceneConstants;
  ComPtr<ID3D11Buffer> compositeConstants;
  ComPtr<ID3D11Buffer> effectConstants;
  ComPtr<ID3D11SamplerState> sampler;
  std::array<Surface, 2> decks;
  std::array<Surface, 2> processed;
  std::array<Surface, 2> history;
  std::array<bool, 2> historyValid{};
  Surface output;
  UINT internalWidth = 0;
  UINT internalHeight = 0;
  double lastPublishedSeconds = -std::numeric_limits<double>::infinity();
  spoutDX sender;
  bool senderOpen = false;

  bool BuildSceneShaders(std::string& error) {
    std::array<ComPtr<ID3D11PixelShader>, kScenes.size()> candidates;
    for (unsigned i = 0; i < kScenes.size(); ++i) {
      const auto file = shaderDirectory / kScenes[i].file;
      std::ifstream stream(file, std::ios::binary);
      if (!stream) {
        error = "Cannot open scene shader: " + file.string();
        return false;
      }
      const std::string source((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
      ComPtr<ID3DBlob> bytecode;
      if (!compile_hlsl(source, file.string(), "main", "ps_5_0", bytecode,
                        error)) return false;
      const HRESULT hr = device->CreatePixelShader(bytecode->GetBufferPointer(),
                                                   bytecode->GetBufferSize(),
                                                   nullptr, &candidates[i]);
      if (FAILED(hr)) {
        error = hr_text("CreatePixelShader " + file.string(), hr);
        return false;
      }
    }
    sceneShaders = std::move(candidates);
    return true;
  }

  bool ResizeDecks(UINT width, UINT height, std::string& error) {
    if (width == internalWidth && height == internalHeight) return true;
    std::array<Surface, 2> nextDecks, nextProcessed, nextHistory;
    for (unsigned i = 0; i < 2; ++i) {
      if (!create_surface(device.Get(), width, height, nextDecks[i], error) ||
          !create_surface(device.Get(), width, height, nextProcessed[i], error) ||
          !create_surface(device.Get(), width, height, nextHistory[i], error)) {
        return false;
      }
    }
    decks = std::move(nextDecks);
    processed = std::move(nextProcessed);
    history = std::move(nextHistory);
    historyValid = {};
    constexpr float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (auto& surface : history) {
      context->ClearRenderTargetView(surface.renderTarget.Get(), black);
    }
    internalWidth = width;
    internalHeight = height;
    return true;
  }
};

SpoutRender::SpoutRender() = default;
SpoutRender::~SpoutRender() { Shutdown(); }

bool SpoutRender::Initialize(const std::filesystem::path& shaderDirectory,
                             std::string& error) {
  Shutdown();
  error.clear();
  auto next = std::make_unique<Impl>();
  next->shaderDirectory = shaderDirectory;
  ComPtr<IDXGIAdapter1> adapter;
  if (!select_rtx_4070(adapter, error)) return false;
  constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                           D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL actual{};
  HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                 static_cast<UINT>(std::size(levels)),
                                 D3D11_SDK_VERSION, &next->device, &actual,
                                 &next->context);
  if (FAILED(hr)) { error = hr_text("D3D11CreateDevice", hr); return false; }

  ComPtr<ID3DBlob> bytecode;
  if (!compile_hlsl(kFullscreenShader, "fullscreen vertex", "main", "vs_5_0",
                    bytecode, error)) return false;
  hr = next->device->CreateVertexShader(bytecode->GetBufferPointer(),
                                        bytecode->GetBufferSize(), nullptr,
                                        &next->vertexShader);
  if (FAILED(hr)) { error = hr_text("CreateVertexShader", hr); return false; }
  bytecode.Reset();
  if (!compile_hlsl(kCompositeShader, "deck composite", "main", "ps_5_0",
                    bytecode, error)) return false;
  hr = next->device->CreatePixelShader(bytecode->GetBufferPointer(),
                                       bytecode->GetBufferSize(), nullptr,
                                       &next->compositeShader);
  if (FAILED(hr)) { error = hr_text("CreatePixelShader composite", hr); return false; }
  bytecode.Reset();
  if (!compile_hlsl(kEffectShader, "deck effects", "main", "ps_5_0",
                    bytecode, error)) return false;
  hr = next->device->CreatePixelShader(bytecode->GetBufferPointer(),
                                       bytecode->GetBufferSize(), nullptr,
                                       &next->effectShader);
  if (FAILED(hr)) { error = hr_text("CreatePixelShader effects", hr); return false; }
  if (!next->BuildSceneShaders(error)) return false;

  D3D11_BUFFER_DESC buffer_desc{};
  buffer_desc.ByteWidth = sizeof(SceneInputs);
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  hr = next->device->CreateBuffer(&buffer_desc, nullptr, &next->sceneConstants);
  if (FAILED(hr)) { error = hr_text("CreateBuffer scene", hr); return false; }
  buffer_desc.ByteWidth = sizeof(CompositeInputs);
  hr = next->device->CreateBuffer(&buffer_desc, nullptr,
                                  &next->compositeConstants);
  if (FAILED(hr)) { error = hr_text("CreateBuffer composite", hr); return false; }
  buffer_desc.ByteWidth = sizeof(EffectInputs);
  hr = next->device->CreateBuffer(&buffer_desc, nullptr,
                                  &next->effectConstants);
  if (FAILED(hr)) { error = hr_text("CreateBuffer effects", hr); return false; }

  D3D11_SAMPLER_DESC sampler_desc{};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  hr = next->device->CreateSamplerState(&sampler_desc, &next->sampler);
  if (FAILED(hr)) { error = hr_text("CreateSamplerState", hr); return false; }

  if (!next->ResizeDecks(kOutputWidth, kOutputHeight, error) ||
      !create_surface(next->device.Get(), kOutputWidth, kOutputHeight,
                      next->output, error)) return false;

  if (!next->sender.OpenDirectX11(next->device.Get()) ||
      !next->sender.SetSenderName(kSenderName.data())) {
    error = "Spout sender initialization failed";
    return false;
  }
  next->senderOpen = true;
  impl_ = std::move(next);
  return true;
}

bool SpoutRender::Render(const ShowSnapshot& show, const SignalFrameV1& signal,
                         const QualityTier& quality, double seconds,
                         std::string& error) {
  error.clear();
  if (!impl_) { error = "Spout renderer is not initialized"; return false; }
  if (!std::isfinite(seconds)) { error = "Render time is not finite"; return false; }
  auto& render = *impl_;
  if (quality.targetFps > 0 && quality.targetFps <= 30 &&
      seconds >= render.lastPublishedSeconds &&
      seconds - render.lastPublishedSeconds < 1.0 / quality.targetFps) {
    return true;
  }

  const float scale = std::isfinite(quality.internalScale)
                          ? std::clamp(quality.internalScale, 0.5f, 1.0f)
                          : 1.0f;
  const auto width = static_cast<UINT>(std::lround(kOutputWidth * scale));
  const auto height = static_cast<UINT>(std::lround(kOutputHeight * scale));
  if (!render.ResizeDecks(width, height, error)) return false;

  render.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  render.context->VSSetShader(render.vertexShader.Get(), nullptr, 0);
  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MaxDepth = 1.0f;
  render.context->RSSetViewports(1, &viewport);

  const float time = static_cast<float>(std::fmod(std::max(seconds, 0.0), 3600.0));
  for (unsigned deck = 0; deck < render.decks.size(); ++deck) {
    SceneInputs inputs{};
    inputs.time = time;
    inputs.bass = finite_unit(signal.bass);
    inputs.mids = finite_unit(signal.mids);
    inputs.highs = finite_unit(signal.highs);
    inputs.hit = signal.hit ? 1.0f : 0.0f;
    inputs.width = static_cast<float>(width);
    inputs.height = static_cast<float>(height);
    render.context->UpdateSubresource(render.sceneConstants.Get(), 0, nullptr,
                                       &inputs, 0, 0);
    render.context->OMSetRenderTargets(1,
                                      render.decks[deck].renderTarget.GetAddressOf(),
                                      nullptr);
    render.context->PSSetConstantBuffers(0, 1,
                                         render.sceneConstants.GetAddressOf());
    render.context->PSSetShader(
        render.sceneShaders[scene_index(show.decks[deck].sceneId)].Get(),
        nullptr, 0);
    render.context->Draw(3, 0);
  }

  // Each deck is processed independently, so its effect settings and feedback
  // history remain distinct while the crossfader moves.
  render.context->PSSetShader(render.effectShader.Get(), nullptr, 0);
  render.context->PSSetSamplers(0, 1, render.sampler.GetAddressOf());
  for (unsigned deck = 0; deck < render.decks.size(); ++deck) {
    EffectInputs effect{};
    effect.bloom = finite_unit(show.decks[deck].effects[0]);
    effect.feedback = finite_unit(show.decks[deck].effects[1]);
    effect.kaleidoscope = finite_unit(show.decks[deck].effects[2]);
    effect.pixelate = finite_unit(show.decks[deck].effects[3]);
    effect.texelX = 1.0f / width;
    effect.texelY = 1.0f / height;
    effect.historyReady = render.historyValid[deck] ? 1.0f : 0.0f;
    render.context->UpdateSubresource(render.effectConstants.Get(), 0, nullptr,
                                       &effect, 0, 0);
    render.context->OMSetRenderTargets(
        1, render.processed[deck].renderTarget.GetAddressOf(), nullptr);
    render.context->PSSetConstantBuffers(0, 1,
                                         render.effectConstants.GetAddressOf());
    ID3D11ShaderResourceView* inputs[2] = {
        render.decks[deck].shaderResource.Get(),
        render.history[deck].shaderResource.Get()};
    render.context->PSSetShaderResources(0, 2, inputs);
    render.context->Draw(3, 0);
    ID3D11ShaderResourceView* unbound[2] = {nullptr, nullptr};
    render.context->PSSetShaderResources(0, 2, unbound);
    render.context->OMSetRenderTargets(0, nullptr, nullptr);
    render.context->CopyResource(render.history[deck].texture.Get(),
                                 render.processed[deck].texture.Get());
    render.historyValid[deck] = true;
  }

  viewport.Width = static_cast<float>(kOutputWidth);
  viewport.Height = static_cast<float>(kOutputHeight);
  render.context->RSSetViewports(1, &viewport);
  render.context->OMSetRenderTargets(1, render.output.renderTarget.GetAddressOf(),
                                    nullptr);
  CompositeInputs composite{};
  composite.crossfader = finite_unit(show.crossfader);
  composite.gain = show.blackout ? 0.0f : (show.panicDim ? 0.2f : 1.0f);
  render.context->UpdateSubresource(render.compositeConstants.Get(), 0, nullptr,
                                     &composite, 0, 0);
  render.context->PSSetConstantBuffers(0, 1,
                                       render.compositeConstants.GetAddressOf());
  render.context->PSSetShader(render.compositeShader.Get(), nullptr, 0);
  ID3D11ShaderResourceView* resources[2] = {
      render.processed[0].shaderResource.Get(),
      render.processed[1].shaderResource.Get()};
  render.context->PSSetShaderResources(0, 2, resources);
  render.context->PSSetSamplers(0, 1, render.sampler.GetAddressOf());
  render.context->Draw(3, 0);
  ID3D11ShaderResourceView* empty[2] = {nullptr, nullptr};
  render.context->PSSetShaderResources(0, 2, empty);

  if (!render.sender.SendTexture(render.output.texture.Get())) {
    error = "Spout SendTexture failed";
    return false;
  }
  render.lastPublishedSeconds = seconds;
  return true;
}

bool SpoutRender::ReloadShaders(std::string& error) {
  error.clear();
  if (!impl_) { error = "Spout renderer is not initialized"; return false; }
  return impl_->BuildSceneShaders(error);
}

void SpoutRender::Shutdown() {
  if (!impl_) return;
  if (impl_->senderOpen) {
    impl_->sender.ReleaseSender();
    impl_->sender.CloseDirectX11();
    impl_->senderOpen = false;
  }
  impl_.reset();
}

bool SpoutRender::IsInitialized() const noexcept { return impl_ != nullptr; }

unsigned SpoutRender::InternalWidth() const noexcept {
  return impl_ ? impl_->internalWidth : 0;
}

unsigned SpoutRender::InternalHeight() const noexcept {
  return impl_ ? impl_->internalHeight : 0;
}

}  // namespace prismforge
