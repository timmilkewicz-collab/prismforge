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
  float wind = lerp(0.2, 1.6, saturate(controls.x + assist * 0.18 * musicalA.y));
  float scale = lerp(4.0, 16.0, controls.y);
  float grain = lerp(0.0, 1.0, controls.z);
  float relief = lerp(0.4, 1.5, saturate(controls.w + assist * 0.12 * (musicalA.z - 0.4)));
  float low = knee(bass);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float dune = sin((p.y * 2.2 + p.x * 0.35) * scale * 0.15 - time * 0.12 * wind);
  dune += 0.45 * sin(p.y * scale + time * 0.08 * wind + low);
  dune += 0.25 * sin(p.x * scale * 0.5 - time * 0.05);
  float ridge = pow(saturate(0.5 + 0.5 * dune), 3.0 * relief);
  float shadow = smoothstep(-0.2, 0.8, dune);
  float grit = hash21(floor(p * (40.0 + 80.0 * grain) + time * wind));
  float3 shadowCol = float3(0.05, 0.02, 0.01);
  float3 sand = float3(0.72, 0.48, 0.18);
  float3 highlight = float3(0.95, 0.82, 0.55);
  float3 color = lerp(shadowCol, sand, shadow);
  color = lerp(color, highlight, ridge * 0.65);
  color = lerp(color, color * (0.7 + 0.5 * grit), grain);
  color *= 0.75 + 0.25 * (1.0 - abs(p.y));
  color += saturate(hit) * ridge * float3(0.08, 0.04, 0.0);
  float centerPres = pf_operator_center_pressure(controls);
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(0.95, 0.75, 0.35), float3(0.55, 0.35, 0.15));
  color = pf_depth_structure(color, p, time, relief, scale * 0.02);
  return float4(saturate(color), 1.0);
}
