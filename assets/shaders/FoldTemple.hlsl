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

float knee(float value) {
  float signal = max(saturate(value) - 0.004, 0.0);
  return signal / (signal + 0.04);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float4 controls = saturate(sceneParams);
  float assist = musicalB.w;
  float folds = lerp(1.0, 5.0, saturate(controls.x + assist * 0.16 * (musicalA.z - 0.3)));
  float crease = lerp(0.4, 1.6, controls.y);
  float turn = (controls.z - 0.5) * 1.2 + time * 0.05;
  float tone = lerp(0.15, 0.9, saturate(controls.w - assist * 0.1 * musicalB.y));
  float low = knee(bass);
  float sn = sin(turn);
  float cs = cos(turn);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  p = float2(cs * p.x - sn * p.y, sn * p.x + cs * p.y);
  float2 q = p;
  [loop] for (int i = 0; i < 5; ++i) {
    if ((float)i >= folds) break;
    q = abs(q) - float2(0.18, 0.12) * (1.0 - 0.12 * (float)i);
    float a = 0.5 + 0.15 * sin(time * 0.2 + (float)i);
    float s = sin(a);
    float c = cos(a);
    q = float2(c * q.x - s * q.y, s * q.x + c * q.y);
  }
  float edge = min(abs(q.x), abs(q.y));
  float creaseMask = 1.0 - smoothstep(0.002, 0.012 * crease, edge);
  float face = smoothstep(-0.2, 0.4, q.x + q.y);
  float3 paper = lerp(float3(0.08, 0.07, 0.12), float3(0.82, 0.78, 0.70), tone);
  float3 ink = float3(0.10, 0.08, 0.28);
  float3 color = lerp(paper * 0.35, paper, face);
  color = lerp(color, ink, creaseMask * crease);
  color += creaseMask * float3(0.15, 0.05, 0.02) * low;
  color *= 0.85 + 0.15 * (1.0 - length(p));
  float centerPres = pf_operator_center_pressure(controls);
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(0.25, 0.12, 0.35), float3(0.75, 0.65, 0.45));
  color = pf_depth_structure(color, p, time, crease, folds * 0.22);
  return float4(saturate(color), 1.0);
}
