#ifndef QUALITY_TIER
#define QUALITY_TIER 0
#endif

#if QUALITY_TIER >= 3
#define POINT_COUNT 10
#elif QUALITY_TIER == 2
#define POINT_COUNT 16
#else
#define POINT_COUNT 24
#endif

cbuffer SceneInputs : register(b0) {
  float time;
  float bass;
  float mids;
  float highs;
  float hit;
  float2 resolution;
  float4 reactive;
  float4 sceneParams;
  float4 musicalA;
  float4 musicalB;
};

struct PSInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

float hash11(float n) {
  return frac(sin(n * 127.1) * 43758.5453);
}

float knee(float value) {
  float signal = max(saturate(value) - 0.004, 0.0);
  return signal / (signal + 0.04);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float4 controls = saturate(sceneParams);
  float assist = musicalB.w;
  float depth = lerp(0.3, 1.4, saturate(controls.x + assist * 0.14 * (musicalA.z - 0.3)));
  float density = lerp(0.45, 1.0, saturate(controls.y + assist * 0.12 * musicalA.y));
  float size = lerp(0.5, 1.6, controls.z);
  float parallax = lerp(0.2, 1.5, controls.w);
  float low = knee(bass);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float3 color = float3(0.0, 0.004, 0.012);
  [loop] for (int i = 0; i < POINT_COUNT; ++i) {
    if (hash11((float)i + 4.0) > density) continue;
    float id = (float)i;
    float z = frac(hash11(id * 3.1) + time * (0.03 + 0.05 * depth) + low * 0.05);
    float layer = 0.25 + z * depth;
    float2 center = float2(
        (hash11(id) - 0.5) * 1.7 + sin(time * 0.2 + id) * 0.05 * parallax * layer,
        (hash11(id + 9.0) - 0.5) * 1.0 + cos(time * 0.17 + id) * 0.04 * parallax * layer);
    float radius = (0.004 + 0.012 * (1.0 - z)) * size;
    float body = exp(-pow(length(p - center) / radius, 2.0));
    float3 tint = lerp(float3(0.15, 0.35, 0.7), float3(0.75, 0.9, 1.0), 1.0 - z);
    float fog = lerp(0.25, 1.0, 1.0 - z);
    color += tint * body * fog;
  }
  color += saturate(hit) * float3(0.02, 0.03, 0.05);
  float centerPres = pf_operator_center_pressure(controls);
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(0.2, 0.45, 0.85), float3(0.75, 0.9, 1.0));
  color = pf_depth_structure(color, p, time, depth, density * 0.45);
  return float4(saturate(color), 1.0);
}
