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

float boxDistance(float2 p, float2 halfSize) {
  float2 d = abs(p) - halfSize;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float knee(float value) {
  float signal = max(saturate(value) - 0.004, 0.0);
  return signal / (signal + 0.04);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float4 controls = saturate(sceneParams);
  float assist = musicalB.w;
  float depthPush = lerp(0.6, 1.6, saturate(controls.x + assist * 0.16 * (musicalA.z - 0.35)));
  float drift = lerp(0.0, 1.0, controls.y);
  float panes = lerp(4.0, 12.0, saturate(controls.z + assist * 0.12 * musicalA.y));
  float glass = lerp(0.35, 1.0, controls.w);
  float low = knee(bass);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float centerPres = pf_operator_center_pressure(controls);
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, centerPres, c0, c1, c2, c3);
  float2 vanishA = lerp(float2(-0.22 * drift, 0.05 * sin(time * 0.11)), c0, centerPres);
  float2 vanishB = lerp(float2(0.28 * drift, -0.04 * cos(time * 0.09)), c1, centerPres);
  float3 color = float3(0.004, 0.006, 0.018);

#if QUALITY_TIER >= 3
  int layers = 5;
#else
  int layers = 9;
#endif
  [loop] for (int i = 0; i < layers; ++i) {
    float depth = frac((float)i / (float)layers + time * (0.04 + 0.02 * low) * depthPush);
    float2 vanish = lerp(vanishA, vanishB, step(0.5, frac((float)i * 0.37)));
    float2 q = (p - vanish) * (0.55 + depth * depth * 1.8);
    float size = 0.08 + depth * 0.85;
    float d = abs(boxDistance(q, float2(size * 1.45, size * 0.82)));
    float frame = 1.0 - smoothstep(0.002, 0.008 + 0.01 * depth, d);
    float pane = abs(sin(q.x * panes + depth * 4.0));
    float glassBand = (1.0 - smoothstep(0.0, 0.08, pane)) * smoothstep(0.02, 0.2, d);
    float3 tint = lerp(float3(0.05, 0.35, 0.72), float3(0.72, 0.16, 0.38), depth);
    color += tint * frame * (0.18 + 0.55 * depth) * glass;
    color += tint * glassBand * 0.08 * (1.0 - depth);
    float recess = exp(-d * (8.0 - 4.0 * depth));
    color += recess * float3(0.01, 0.02, 0.04);
  }

  float2 floorP = p - vanishA * 0.3;
  float horizon = abs(floorP.y);
  float facet = abs(frac((floorP.x / (horizon + 0.05)) * 1.6) - 0.5);
  float stripe = 1.0 - smoothstep(0.0, 0.045, facet);
  color += stripe * smoothstep(0.12, 0.5, horizon) * float3(0.03, 0.07, 0.10);
  color += saturate(hit) * float3(0.02, 0.03, 0.04);
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(0.1, 0.4, 0.75), float3(0.75, 0.2, 0.45));
  color = pf_depth_structure(color, p, time, depthPush, drift * 0.35);
  return float4(saturate(color), 1.0);
}
