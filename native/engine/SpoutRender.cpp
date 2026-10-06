#include "SpoutRender.h"
#include "PrismForge/ReactiveMotion.h"

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
#include <utility>

namespace prismforge {
namespace {

using Microsoft::WRL::ComPtr;
constexpr DXGI_FORMAT kPixelFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr unsigned kQualityTierCount = 4;
constexpr std::array<const char*, kQualityTierCount> kQualityTierDefines = {
    "0", "1", "2", "3"};

struct SceneSpec {
  std::string_view id;
  std::string_view file;
};

constexpr std::array<SceneSpec, 18> kScenes = {{
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
    {"hex-vortex", "HexVortex.hlsl"},
    {"ferrofluid-reactor", "FerrofluidReactor.hlsl"},
    {"shardwell", "Shardwell.hlsl"},
    {"neon-orbs", "NeonOrbs.hlsl"},
    {"mirror-cathedral", "MirrorCathedral.hlsl"},
    {"recursive-circuit", "RecursiveCircuit.hlsl"},
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
  float flowEchoStrength;
  float warp;
  float trails;
  float colorEnergy;
  float motionTime;
  float bass;
  float reactiveEnergy;
  float reactivePulse;
  float reactiveFlow;
};
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
float2 rotate2D(float2 p, float angle) {
  float s = sin(angle);
  float c = cos(angle);
  return float2(c * p.x - s * p.y, s * p.x + c * p.y);
}
float2 effect_uv(float2 uv) {
  if (warp > 0.001) {
    float2 centered = uv - 0.5;
    float radius = length(centered);
    float twist = warp * (0.16 + 0.05 * bass) *
        sin(radius * 18.0 - motionTime * 1.8);
    float2 flow = float2(
        sin(centered.y * 12.0 + motionTime * 1.4),
        cos(centered.x * 10.0 - motionTime * 1.1));
    uv = 0.5 + rotate2D(centered, twist) + flow * (0.025 * warp);
  }
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
  if ((feedback > 0.001 || trails > 0.001) && historyReady > 0.5) {
    float2 historyUv = input.uv;
    if (trails > 0.001) {
      float angle = trails * (0.014 + 0.010 * sin(motionTime * 0.47));
      historyUv = saturate(0.5 + rotate2D(input.uv - 0.5, angle) *
          (1.0 - 0.021 * trails));
    }
    // Recursive Circuit can gently carry its own history along a smooth
    // music-driven curl. Feedback remains the explicit opt-in; trails alone
    // and every other scene retain their original sampling coordinates.
    if (feedback > 0.001 && flowEchoStrength > 0.001) {
      float aspect = texel.y / max(texel.x, 0.000001);
      float2 p = (historyUv - 0.5) * float2(aspect, 1.0);
      float phaseX = p.x * 6.0 + reactiveFlow * 0.44;
      float phaseY = p.y * 6.0 - reactiveFlow * 0.32;
      float2 curl = float2(sin(phaseX) * cos(phaseY) / aspect,
                            -cos(phaseX) * sin(phaseY));
      float drive = saturate(0.15 + 0.65 * saturate(reactiveEnergy) +
                             0.35 * saturate(reactivePulse));
      float travel = min(0.005, 0.005 * saturate(flowEchoStrength) *
                         saturate(feedback) * drive);
      historyUv = saturate(historyUv + curl * travel);
    }
    float3 previous = historyTexture.Sample(linearSampler, historyUv).rgb;
    // Always retain some current scene contribution, even when both controls
    // are at full strength; otherwise history can decay the image to black.
    float amount = min(0.94, saturate(feedback) * 0.88 + trails * 0.76);
    // Multiplicative decay keeps the feedback history from accumulating light.
    color = lerp(color, previous * (1.0 - 0.11 * trails), amount);
  }
  if (colorEnergy > 0.001) {
    float3 rotated = lerp(color.gbr, color.brg,
                          0.5 + 0.5 * sin(motionTime * 0.55));
    color = lerp(color, rotated, 0.38 * colorEnergy);
    // Lift dark detail without a hard global brightness pulse.
    color = lerp(color, sqrt(saturate(color)), 0.38 * colorEnergy);
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = luma + (color - luma) * (1.0 + 0.70 * colorEnergy);
    color = (color - 0.11) * (1.0 + 0.18 * colorEnergy) + 0.11;
  }
  return float4(saturate(color), 1.0);
}
)hlsl";

// SceneInputs matches the HLSL cbuffer b0 in assets/shaders. The first 64
// bytes are the established shader ABI. Musical Recursive Circuit data is
// appended so every existing scene keeps the exact same prefix.
struct alignas(16) SceneInputs {
  float time, bass, mids, highs;
  float hit, width, height, unused0;
  float reactiveEnergy, reactivePulse, reactiveFlow, reactivePalette;
  std::array<float, 4> sceneParams{};
  float musicalFlow, musicalDensity, musicalTopology, musicalPalette;
  float musicalImpact, musicalRelease, musicalVariation, musicalEnabled;
};
static_assert(sizeof(SceneInputs) == 96);

struct alignas(16) CompositeInputs {
  float crossfader, gain, pad0, pad1;
};
static_assert(sizeof(CompositeInputs) == 16);

struct alignas(16) EffectInputs {
  float bloom, feedback, kaleidoscope, pixelate;
  float texelX, texelY, historyReady, flowEchoStrength;
  float warp, trails, colorEnergy, motionTime;
  float bass, reactiveEnergy, reactivePulse, reactiveFlow;
};
static_assert(sizeof(EffectInputs) == 64);

float finite_unit(float value) {
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float finite_signed_unit(float value) {
  return std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
}

float wrap_unit(float value) {
  if (!std::isfinite(value)) return 0.0f;
  value -= std::floor(value);
  return value < 0.0f ? value + 1.0f : value;
}

float event_unit(std::uint64_t value) {
  // SplitMix64's finalizer gives the deterministic engine event ID another
  // stable permutation without introducing renderer RNG state.
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  value ^= value >> 31;
  return static_cast<float>(value >> 40) / 16777215.0f;
}

struct RecursiveSceneFrame {
  float flow = 0.0f;
  float density = 0.0f;
  float topology = 0.0f;
  float palette = 0.0f;
  float impact = 0.0f;
  float release = 0.0f;
  float variation = 0.0f;
};

// Scene-specific interpretation stays in the renderer. It integrates a
// continuous flow clock and eases event-selected palette/topology variation, but
// consumes only MusicalStateFrameV1 -- never raw bands or hit flags.
class RecursiveSceneAdapter {
 public:
  void Reset() noexcept {
    lastSeconds_ = std::numeric_limits<double>::quiet_NaN();
    flow_ = 0.0;
    palette_ = 0.0f;
    paletteTarget_ = 0.0f;
    variation_ = 0.0f;
    variationTarget_ = 0.0f;
    lastEventId_ = 0;
    lastSourceSampleIndex_ = 0;
  }

  [[nodiscard]] RecursiveSceneFrame Advance(
      const MusicalStateFrameV1& musical, double seconds) noexcept {
    if (!std::isfinite(seconds)) return {};
    if (musical.sourceSampleIndex < lastSourceSampleIndex_ ||
        (std::isfinite(lastSeconds_) && seconds < lastSeconds_)) {
      Reset();
    }
    const double deltaSeconds = std::isfinite(lastSeconds_)
        ? std::clamp(seconds - lastSeconds_, 0.0, 0.25) : 0.0;
    lastSeconds_ = seconds;
    lastSourceSampleIndex_ = musical.sourceSampleIndex;

    const float immediate = finite_unit(musical.immediateEnergy);
    const float onset = finite_unit(musical.onsetEnvelope);
    const float accent = finite_unit(musical.accentEnvelope);
    const float groove = finite_unit(musical.grooveEnergy);
    const float sustained = finite_unit(musical.sustainedEnergy);
    const float trend = finite_signed_unit(musical.energyTrend);
    const float calm = finite_unit(musical.calm);
    const float building = finite_unit(musical.building);
    const float driving = finite_unit(musical.driving);
    const float peak = finite_unit(musical.peak);
    const float release = finite_unit(musical.release);
    const float slowAge = finite_unit(musical.slowStateAge);

    if (musical.eventId != 0 && musical.eventId != lastEventId_) {
      variationTarget_ = event_unit(musical.eventId);
      paletteTarget_ = wrap_unit(
          palette_ + 0.17f + 0.46f * variationTarget_);
      lastEventId_ = musical.eventId;
    }

    const float eventResponse = static_cast<float>(1.0 - std::exp(
        -deltaSeconds * (1.4 + 2.2 * building + 3.0 * peak)));
    variation_ += (variationTarget_ - variation_) * eventResponse;
    float paletteDelta = paletteTarget_ - palette_;
    if (paletteDelta > 0.5f) paletteDelta -= 1.0f;
    if (paletteDelta < -0.5f) paletteDelta += 1.0f;
    palette_ = wrap_unit(palette_ + paletteDelta * eventResponse);

    const float density = finite_unit(
        0.04f + 0.18f * immediate + 0.32f * groove +
        0.30f * sustained + 0.18f * building + 0.24f * driving +
        0.28f * peak - 0.22f * calm);
    const float topology = finite_unit(
        0.08f + 0.18f * groove + 0.42f * building +
        0.30f * driving + 0.44f * peak + 0.13f * variation_ -
        0.12f * calm + 0.08f * std::max(trend, 0.0f));
    const float impact = finite_unit(
        0.72f * onset + 0.42f * accent + 0.34f * peak);
    const float velocity = std::max(0.025f,
        (0.10f + 0.72f * groove + 0.34f * sustained +
         0.34f * building + 0.62f * driving + 0.86f * peak +
         0.20f * release) * (1.0f - 0.58f * calm));
    // Do not modulo a visual phase that the shader multiplies by several
    // non-commensurate frequencies. Wrapping would hard-cut the topology at
    // the boundary during a long-running show. This mirrors the established
    // renderer clock policy below: eventual float precision loss is safer
    // than a deterministic whole-frame discontinuity.
    flow_ += deltaSeconds * velocity;
    if (!std::isfinite(flow_) || flow_ < 0.0) flow_ = 0.0;

    RecursiveSceneFrame frame;
    frame.flow = static_cast<float>(flow_);
    frame.density = density;
    frame.topology = topology;
    frame.palette = wrap_unit(palette_ + 0.055f * building +
                              0.11f * peak + 0.035f * slowAge);
    frame.impact = impact;
    frame.release = release;
    frame.variation = finite_unit(variation_);
    return frame;
  }

 private:
  double lastSeconds_ = std::numeric_limits<double>::quiet_NaN();
  double flow_ = 0.0;
  float palette_ = 0.0f;
  float paletteTarget_ = 0.0f;
  float variation_ = 0.0f;
  float variationTarget_ = 0.0f;
  std::uint64_t lastEventId_ = 0;
  std::uint64_t lastSourceSampleIndex_ = 0;
};

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
                  ComPtr<ID3DBlob>& bytecode, std::string& error,
                  const D3D_SHADER_MACRO* macros = nullptr) {
  ComPtr<ID3DBlob> compiler_errors;
  const HRESULT hr = D3DCompile(source.data(), source.size(), label.c_str(),
                                macros, nullptr, entry, profile,
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

unsigned quality_tier_index(const QualityTier& quality) {
  if (quality.targetFps <= 30) return 3;
  if (quality.internalScale <= 0.70f) return 2;
  if (quality.internalScale <= 0.875f) return 1;
  return 0;
}

}  // namespace

struct SpoutRender::Impl {
  std::filesystem::path shaderDirectory;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11VertexShader> vertexShader;
  ComPtr<ID3D11PixelShader> compositeShader;
  ComPtr<ID3D11PixelShader> effectShader;
  using SceneVariants = std::array<ComPtr<ID3D11PixelShader>, kQualityTierCount>;
  std::array<SceneVariants, kScenes.size()> sceneShaders;
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
  double lastSceneSeconds = std::numeric_limits<double>::quiet_NaN();
  double sceneClock = 0.0;
  ReactiveMotion reactiveMotion;
  RecursiveSceneAdapter recursiveScene;
  bool motionEverActive = false;
  std::array<float, 4> smoothedMacros{};
  spoutDX sender;
  bool senderOpen = false;

  bool BuildSceneShaders(std::string& error) {
    std::array<SceneVariants, kScenes.size()> candidates;
    for (unsigned i = 0; i < kScenes.size(); ++i) {
      const auto file = shaderDirectory / kScenes[i].file;
      std::ifstream stream(file, std::ios::binary);
      if (!stream) {
        error = "Cannot open scene shader: " + file.string();
        return false;
      }
      const std::string source((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
      for (unsigned tier = 0; tier < kQualityTierCount; ++tier) {
        const D3D_SHADER_MACRO macros[] = {
            {"QUALITY_TIER", kQualityTierDefines[tier]}, {nullptr, nullptr}};
        const std::string label = file.string() + " tier " + std::to_string(tier);
        ComPtr<ID3DBlob> bytecode;
        if (!compile_hlsl(source, label, "main", "ps_5_0", bytecode,
                          error, macros)) return false;
        const HRESULT hr = device->CreatePixelShader(
            bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr,
            &candidates[i][tier]);
        if (FAILED(hr)) {
          error = hr_text("CreatePixelShader " + label, hr);
          return false;
        }
      }
    }
    // Commit the complete scene/tier set only after every candidate succeeds.
    // A failed hot reload leaves the last valid variants on air.
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

SpoutRender::SpoutRender(std::string senderName)
    : senderName_(std::move(senderName)) {}
SpoutRender::~SpoutRender() { Shutdown(); }

bool SpoutRender::Initialize(const std::filesystem::path& shaderDirectory,
                             std::string& error) {
  Shutdown();
  error.clear();
  if (senderName_.empty()) {
    error = "Spout sender name is empty";
    return false;
  }
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
      !next->sender.SetSenderName(senderName_.c_str())) {
    error = "Spout sender initialization failed";
    return false;
  }
  next->senderOpen = true;
  impl_ = std::move(next);
  return true;
}

bool SpoutRender::Render(const ShowSnapshot& show, const SignalFrameV1& signal,
                         const MusicalStateFrameV1& musicalState,
                         bool useMusicalRecursiveAudio,
                         const QualityTier& quality, double seconds,
                         std::string& error) {
  error.clear();
  if (!impl_) { error = "Spout renderer is not initialized"; return false; }
  if (!std::isfinite(seconds)) { error = "Render time is not finite"; return false; }
  auto& render = *impl_;
  // Advance even when the safety tier skips publishing this iteration. A
  // short onset must not disappear merely because it landed between 30 Hz
  // Spout frames.
  const ReactiveFrame reactive = render.reactiveMotion.Advance(signal, seconds);
  const RecursiveSceneFrame recursiveMusical =
      render.recursiveScene.Advance(musicalState, seconds);
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

  // Speed is integrated rather than multiplying elapsed time. Moving the
  // Motion control therefore changes velocity without jumping scene phase.
  const double deltaSeconds = std::isfinite(render.lastSceneSeconds)
      ? std::max(0.0, seconds - render.lastSceneSeconds) : 0.0;
  if (!std::isfinite(render.lastSceneSeconds)) render.sceneClock = seconds;
  render.lastSceneSeconds = seconds;
  const float smoothing = static_cast<float>(1.0 -
      std::exp(-std::min(deltaSeconds, 0.25) * 8.0));
  for (unsigned index = 0; index < render.smoothedMacros.size(); ++index) {
    const float target = finite_unit(show.masterEffects[index]);
    render.smoothedMacros[index] +=
        (target - render.smoothedMacros[index]) * smoothing;
  }
  if (render.motionEverActive || show.masterEffects[0] > 0.0f ||
      render.smoothedMacros[0] > 0.0f) {
    render.motionEverActive = true;
    render.sceneClock += deltaSeconds *
        (1.0 + 4.0 * render.smoothedMacros[0]);
  } else {
    // Retain the established time value exactly when the macro is untouched.
    render.sceneClock = seconds;
  }

  render.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  render.context->VSSetShader(render.vertexShader.Get(), nullptr, 0);
  D3D11_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MaxDepth = 1.0f;
  render.context->RSSetViewports(1, &viewport);

  // A timed modulo makes non-periodic scene shaders jump (and can flash the
  // whole output). Preserve phase across long performances; gradual float
  // precision loss is preferable to a hard one-hour discontinuity.
  const float time = static_cast<float>(std::max(render.sceneClock, 0.0));
  for (unsigned deck = 0; deck < render.decks.size(); ++deck) {
    const bool musicalRecursive = useMusicalRecursiveAudio &&
        show.decks[deck].sceneId == "recursive-circuit";
    SceneInputs inputs{};
    inputs.time = time;
    inputs.bass = finite_unit(signal.bass);
    inputs.mids = finite_unit(signal.mids);
    inputs.highs = finite_unit(signal.highs);
    inputs.hit = signal.hit ? 1.0f : 0.0f;
    inputs.width = static_cast<float>(width);
    inputs.height = static_cast<float>(height);
    inputs.reactiveEnergy = reactive.energy;
    inputs.reactivePulse = reactive.pulse;
    inputs.reactiveFlow = reactive.flow;
    inputs.reactivePalette = reactive.palette;
    inputs.sceneParams = show.decks[deck].sceneParams;
    if (musicalRecursive) {
      inputs.musicalFlow = recursiveMusical.flow;
      inputs.musicalDensity = recursiveMusical.density;
      inputs.musicalTopology = recursiveMusical.topology;
      inputs.musicalPalette = recursiveMusical.palette;
      inputs.musicalImpact = recursiveMusical.impact;
      inputs.musicalRelease = recursiveMusical.release;
      inputs.musicalVariation = recursiveMusical.variation;
      inputs.musicalEnabled = 1.0f;
    }
    render.context->UpdateSubresource(render.sceneConstants.Get(), 0, nullptr,
                                       &inputs, 0, 0);
    render.context->OMSetRenderTargets(1,
                                      render.decks[deck].renderTarget.GetAddressOf(),
                                      nullptr);
    render.context->PSSetConstantBuffers(0, 1,
                                         render.sceneConstants.GetAddressOf());
    render.context->PSSetShader(
        render.sceneShaders[scene_index(show.decks[deck].sceneId)]
                           [quality_tier_index(quality)].Get(),
        nullptr, 0);
    render.context->Draw(3, 0);
  }

  // Each deck is processed independently, so its effect settings and feedback
  // history remain distinct while the crossfader moves.
  render.context->PSSetShader(render.effectShader.Get(), nullptr, 0);
  render.context->PSSetSamplers(0, 1, render.sampler.GetAddressOf());
  for (unsigned deck = 0; deck < render.decks.size(); ++deck) {
    const bool musicalRecursive = useMusicalRecursiveAudio &&
        show.decks[deck].sceneId == "recursive-circuit";
    EffectInputs effect{};
    effect.bloom = finite_unit(show.decks[deck].effects[0]);
    effect.feedback = finite_unit(show.decks[deck].effects[1]);
    effect.kaleidoscope = finite_unit(show.decks[deck].effects[2]);
    effect.pixelate = finite_unit(show.decks[deck].effects[3]);
    effect.texelX = 1.0f / width;
    effect.texelY = 1.0f / height;
    effect.historyReady = render.historyValid[deck] ? 1.0f : 0.0f;
    effect.flowEchoStrength = show.decks[deck].sceneId == "recursive-circuit"
        ? finite_unit(show.decks[deck].sceneParams[1]) : 0.0f;
    effect.warp = render.smoothedMacros[1];
    effect.trails = render.smoothedMacros[2];
    effect.colorEnergy = render.smoothedMacros[3];
    effect.motionTime = time;
    effect.bass = finite_unit(signal.bass);
    effect.reactiveEnergy = musicalRecursive
        ? std::max(recursiveMusical.density,
                   0.55f * recursiveMusical.release)
        : reactive.energy;
    effect.reactivePulse = musicalRecursive
        ? recursiveMusical.impact : reactive.pulse;
    effect.reactiveFlow = musicalRecursive
        ? recursiveMusical.flow : reactive.flow;
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

void SpoutRender::ResetMusicalSceneState() noexcept {
  if (impl_) impl_->recursiveScene.Reset();
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
