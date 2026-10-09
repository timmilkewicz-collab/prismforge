#ifndef QUALITY_TIER
#define QUALITY_TIER 0
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

float hash21(float2 p) {
  return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float knee(float value) {
  float signal = max(saturate(value) - 0.004, 0.0);
  return signal / (signal + 0.04);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float4 controls = saturate(sceneParams);
  float assist = musicalB.w;
  float feed = lerp(0.6, 1.8, saturate(controls.x + assist * 0.18 * (musicalA.z - 0.3)));
  float scale = lerp(3.5, 9.0, controls.y);
  float heat = lerp(0.2, 1.0, saturate(controls.z + assist * 0.2 * musicalB.x));
  float membrane = lerp(0.3, 1.2, saturate(controls.w + assist * 0.1 * musicalB.y));
  float low = knee(bass);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float2 drift = float2(time * 0.05 * feed, time * 0.03);
  float3 color = float3(0.02, 0.005, 0.015);

#if QUALITY_TIER >= 3
  int cells = 5;
#else
  int cells = 8;
#endif
  [loop] for (int i = 0; i < cells; ++i) {
    float2 seed = float2(hash21(float2(i, 2.0)), hash21(float2(i, 9.0))) - 0.5;
    float2 center = seed * float2(1.3, 0.8) + 0.08 * float2(sin(time * 0.2 + i), cos(time * 0.17 + i));
    float radius = (0.06 + 0.04 * hash21(float2(i, 4.0))) * (0.7 + 0.5 * low) * feed;
    float dist = length((p - drift * 0.2) - center) * scale;
    float body = exp(-pow(dist / max(radius * scale, 0.01), 2.0));
    float wall = exp(-pow((dist - radius * scale) * (6.0 * membrane), 2.0));
    float split = 0.5 + 0.5 * sin(time * 0.3 + i + musicalA.w * 6.28);
    float3 hot = lerp(float3(0.15, 0.55, 0.25), float3(0.95, 0.28, 0.05), heat * split);
    color += hot * body * 0.55;
    color += float3(0.95, 0.85, 0.55) * wall * 0.35;
  }
  float centerPres = pf_operator_center_pressure(controls);
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, centerPres, c0, c1, c2, c3);
  color += exp(-length(p - c0) * 10.0) * float3(0.2, 0.85, 0.35) * centerPres * feed;
  color += exp(-length(p - c2) * 9.0) * float3(0.95, 0.35, 0.08) * centerPres;
  color = pf_center_rim(color, p, time, centerPres, float3(0.2, 0.75, 0.35), float3(0.95, 0.45, 0.1));
  color = pf_depth_structure(color, p, time, scale, heat * 0.35);
  return float4(saturate(color), 1.0);
}
