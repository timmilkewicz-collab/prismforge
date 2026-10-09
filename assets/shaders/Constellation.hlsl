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

float segmentDistance(float2 p, float2 a, float2 b) {
  float2 v = b - a;
  float t = saturate(dot(p - a, v) / max(dot(v, v), 0.0001));
  return length(p - (a + t * v));
}

float knee(float value) {
  float signal = max(saturate(value) - 0.004, 0.0);
  return signal / (signal + 0.04);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float4 controls = saturate(sceneParams);
  float assist = musicalB.w;
  float stars = lerp(6.0, 16.0, saturate(controls.x + assist * 0.12 * musicalA.y));
  float links = lerp(0.08, 0.42, saturate(controls.y + assist * 0.14 * (musicalA.z - 0.35)));
  float drift = lerp(0.2, 1.4, controls.z);
  float twinkle = lerp(0.2, 1.0, saturate(controls.w + assist * 0.2 * musicalB.x));
  float high = knee(highs);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float3 color = float3(0.004, 0.006, 0.018);
  float2 previous = float2(0.0, 0.0);

#if QUALITY_TIER >= 3
  int count = 7;
#else
  int count = 14;
#endif
  [loop] for (int i = 0; i < count; ++i) {
    if ((float)i >= stars) break;
    float id = (float)i;
    float2 center = float2(
        (hash21(float2(id, 1.0)) - 0.5) * 1.5,
        (hash21(float2(id, 3.0)) - 0.5) * 0.9);
    center += 0.06 * drift * float2(sin(time * 0.2 + id), cos(time * 0.16 + id * 1.3));
    float tw = 0.55 + 0.45 * sin(time * (1.5 + id * 0.2) + id) * twinkle + high;
    float body = exp(-length(p - center) * lerp(40.0, 90.0, twinkle));
    float3 tint = lerp(float3(0.75, 0.82, 1.0), float3(1.0, 0.82, 0.45), hash21(float2(id, 8.0)));
    color += tint * body * tw;
    if (i > 0 && length(center - previous) < links) {
      float link = 1.0 - smoothstep(0.001, 0.004, segmentDistance(p, previous, center));
      color += link * float3(0.35, 0.45, 0.7) * 0.45;
    }
    previous = center;
  }
  color += saturate(hit) * float3(0.03, 0.03, 0.02);
  float centerPres = pf_operator_center_pressure(controls);
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, centerPres, c0, c1, c2, c3);
  color += exp(-length(p - c0) * 12.0) * float3(0.35, 0.45, 0.95) * centerPres;
  color += exp(-length(p - c1) * 11.0) * float3(0.95, 0.55, 0.25) * centerPres;
  color += exp(-length(p - c2) * 10.0) * float3(0.4, 0.85, 0.55) * centerPres;
  color += exp(-length(p - c3) * 10.5) * float3(0.75, 0.35, 0.85) * centerPres;
  color = pf_center_rim(color, p, time, centerPres, float3(0.5, 0.6, 1.0), float3(1.0, 0.7, 0.35));
  color = pf_depth_structure(color, p, time, links, drift * 0.3);
  return float4(saturate(color), 1.0);
}
