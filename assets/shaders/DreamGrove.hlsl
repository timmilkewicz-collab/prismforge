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
  float growth = lerp(0.4, 1.3, saturate(controls.x + assist * 0.16 * musicalA.y));
  float branch = lerp(0.3, 1.0, saturate(controls.y + assist * 0.18 * (musicalA.z - 0.35)));
  float canopy = lerp(0.2, 1.0, controls.z);
  float glow = lerp(0.2, 1.2, saturate(controls.w + assist * 0.15 * musicalB.x));
  float low = knee(bass);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float3 color = float3(0.004, 0.012, 0.008);
  float2 roots[2] = {float2(-0.35, 0.35), float2(0.4, 0.28)};
  [unroll] for (int r = 0; r < 2; ++r) {
    float2 node = roots[r];
    [unroll] for (int i = 0; i < 6; ++i) {
      float ang = sin(time * 0.15 + (float)i + (float)r * 2.0) * branch + (float)i * 0.7;
      float len = (0.12 + 0.03 * (float)i) * growth;
      float2 next = node + float2(cos(ang), -abs(sin(ang))) * len;
      float limb = 1.0 - smoothstep(0.002, 0.008, segmentDistance(p, node, next));
      color += limb * lerp(float3(0.15, 0.08, 0.03), float3(0.05, 0.35, 0.12), (float)i / 5.0);
      float leaf = exp(-length(p - next) * (18.0 - 8.0 * canopy));
      color += leaf * float3(0.05, 0.45, 0.18) * canopy;
      node = next;
    }
  }
  float spore = pow(saturate(0.5 + 0.5 * sin(p.x * 30.0 + time) * sin(p.y * 24.0 - time * 0.7)), 20.0);
  color += spore * float3(0.6, 0.85, 0.3) * glow * (0.3 + low);
  color += saturate(hit) * spore * float3(0.1, 0.1, 0.0);
  float centerPres = pf_operator_center_pressure(controls);
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(0.15, 0.55, 0.22), float3(0.55, 0.85, 0.35));
  color = pf_depth_structure(color, p, time, growth, branch * 0.35);
  return float4(saturate(color), 1.0);
}
