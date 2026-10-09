#ifndef QUALITY_TIER
#define QUALITY_TIER 0
#endif

#if QUALITY_TIER >= 3
#define POINT_COUNT 8
#elif QUALITY_TIER == 2
#define POINT_COUNT 12
#elif QUALITY_TIER == 1
#define POINT_COUNT 16
#else
#define POINT_COUNT 22
#endif

cbuffer SceneInputs : register(b0) {
  float time;
  float bass;
  float mids;
  float highs;
  float hit;
  float2 resolution;
  float2 pad;
};

struct PSInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float3 color = float3(0.004, 0.017, 0.029);
  float radiusScale = 0.82 + 0.27 * bass;
  float spin = time * 0.18;

  // A rotating Fibonacci sphere gives each dot coherent three-dimensional
  // motion without a geometry pass or an unbounded particle simulation.
  [unroll] for (int i = 0; i < POINT_COUNT; ++i) {
    float seed = (float)i;
    float y = 1.0 - 2.0 * (seed + 0.5) / (float)POINT_COUNT;
    float ring = sqrt(saturate(1.0 - y * y));
    float angle = seed * 2.399963 + spin;
    float3 sphere = float3(cos(angle) * ring, y,
                           sin(angle) * ring);
    sphere.xy *= radiusScale;
    float perspective = 0.34 / (1.35 - sphere.z * 0.35);
    float2 projected = sphere.xy * perspective;
    float distanceToDot = length(p - projected);
    float dotSize = 0.004 + 0.004 * (sphere.z + 1.0);
    float core = 1.0 - smoothstep(dotSize * 0.45,
                                  dotSize, distanceToDot);
    float halo = 0.002 / (0.002 + distanceToDot * distanceToDot * 170.0);
    float depthLight = 0.4 + 0.6 * saturate(sphere.z * 0.5 + 0.5);
    float3 hue = lerp(float3(0.12, 0.45, 0.60),
                      float3(0.70, 0.38, 0.90), seed / POINT_COUNT);
    color += hue * (core * 0.55 + halo * 0.035) *
             depthLight * (0.7 + 0.3 * highs);
  }

  float shell = abs(length(p) - 0.31 * radiusScale);
  color += float3(0.012, 0.046, 0.059) *
           (1.0 - smoothstep(0.002, 0.008, shell)) *
           (0.4 + mids * 0.3);
  color *= 1.0 + 0.06 * hit;
  return float4(saturate(color), 1.0);
}
