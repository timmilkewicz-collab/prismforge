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

float noise2(float2 p) {
  float2 i = floor(p);
  float2 f = frac(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = lerp(hash21(i), hash21(i + float2(1.0, 0.0)), f.x);
  float b = lerp(hash21(i + float2(0.0, 1.0)), hash21(i + 1.0), f.x);
  return lerp(a, b, f.y);
}

float knee(float value) {
  float signal = max(saturate(value) - 0.004, 0.0);
  return signal / (signal + 0.04);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float4 controls = saturate(sceneParams);
  float assist = musicalB.w;
  float flow = lerp(0.35, 1.8, saturate(controls.x + assist * 0.16 * (musicalA.y - 0.35)));
  float density = lerp(0.55, 1.7, saturate(controls.y + assist * 0.18 * (musicalA.z - 0.4)));
  float tide = lerp(0.15, 0.72, controls.z);
  float contrast = lerp(0.65, 1.8, saturate(controls.w + assist * 0.12 * musicalB.y));
  float low = knee(bass);
  float mid = knee(mids);

  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float centerPres = pf_operator_center_pressure(controls);
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, centerPres, c0, c1, c2, c3);
  float2 wellA = lerp(float2(-tide, 0.08 * sin(time * 0.17)), c0, centerPres);
  float2 wellB = lerp(float2(tide * 0.85, -0.12 * cos(time * 0.13)), c1, centerPres);
  float2 pull = normalize(wellA - p + 0.0001) / (0.18 + length(p - wellA));
  pull += normalize(wellB - p + 0.0001) / (0.22 + length(p - wellB));
  if (centerPres > 0.08) {
    pull += normalize(c2 - p + 0.0001) / (0.20 + length(p - c2)) * centerPres;
    pull += normalize(c3 - p + 0.0001) / (0.24 + length(p - c3)) * centerPres * 0.9;
  }
  float2 q = pf_multi_center_field(p + pull * (0.045 + 0.03 * low) * flow, time,
                                   centerPres * 0.85, 1.0);

  float drift = time * (0.08 + 0.05 * mid) * flow;
  float current = noise2(q * (2.4 * density) + float2(drift, -drift * 0.7));
  float2 w = q + (0.10 + 0.08 * controls.x) * float2(
      sin(q.y * 8.0 + current * 5.0 + drift * 2.0),
      cos(q.x * 7.0 - current * 4.0 - drift));
  float marbling = sin(w.x * (9.0 * density) + sin(w.y * 7.0 - drift) * 2.4);
  marbling += 0.45 * sin(length(w - wellA) * 18.0 - time * 0.4 * flow);
  marbling += 0.38 * sin(length(w - wellB) * 14.0 + time * 0.33 * flow);
  float ink = smoothstep(-0.15, 0.55, marbling);
  float filament = 1.0 - smoothstep(0.015, 0.09, abs(marbling));
  float bridge = 1.0 - smoothstep(0.012, 0.05, abs(dot(normalize(wellB - wellA), q - wellA)));
  bridge *= smoothstep(0.0, 0.15, length(q - wellA)) * smoothstep(0.0, 0.15, length(wellB - q));

  float3 abyss = float3(0.004, 0.012, 0.04);
  float3 indigo = float3(0.05, 0.04, 0.22);
  float3 teal = float3(0.02, 0.62, 0.58);
  float3 color = lerp(abyss, indigo, ink);
  color = lerp(color, teal, saturate(filament * (0.45 + 0.4 * mid) + bridge * 0.7));
  color += filament * float3(0.05, 0.16, 0.18);
  float pool = exp(-length(q - wellA) * 7.0) + exp(-length(q - wellB) * 6.0);
  color += pool * float3(0.0, 0.08, 0.10);
  float mono = dot(color, float3(0.2, 0.7, 0.1));
  color = lerp(mono.xxx, color, contrast);
  color *= 0.92 + 0.08 * sin(w.y * 4.0 + drift);
  color += saturate(hit) * filament * float3(0.04, 0.08, 0.08);
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(0.0, 0.35, 0.42), float3(0.22, 0.05, 0.38));
  color = pf_depth_structure(color, q, time, density, flow * 0.35);
  return float4(saturate(color), 1.0);
}
