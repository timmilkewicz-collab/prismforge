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
  float rings = lerp(3.0, 9.0, saturate(controls.x + assist * 0.14 * musicalA.y));
  float spin = lerp(0.15, 1.3, saturate(controls.y + assist * 0.16 * (musicalA.z - 0.3)));
  float pigment = lerp(0.2, 1.0, controls.z);
  float well = lerp(0.2, 1.2, saturate(controls.w - assist * 0.12 * musicalB.y));
  float low = knee(bass);
  float2 origin = float2(0.16 * sin(time * 0.12), 0.1 * cos(time * 0.1));
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0) - origin;
  float radius = length(p);
  float angle = atan2(p.y, p.x) + time * spin;
  float3 color = float3(0.03, 0.012, 0.015);

  [loop] for (int i = 0; i < 8; ++i) {
    if ((float)i >= rings) break;
    float band = abs(radius - (0.08 + (float)i * 0.07) - 0.02 * sin(angle * (3.0 + (float)i) + time));
    float ring = 1.0 - smoothstep(0.002, 0.01, band);
    float glyph = pow(saturate(0.5 + 0.5 * sin(angle * (5.0 + (float)i) - radius * 10.0)), 12.0);
    float3 gold = float3(0.85, 0.55, 0.08);
    float3 violet = float3(0.42, 0.08, 0.55);
    color += lerp(gold, violet, (float)i / 7.0) * ring * (0.35 + pigment);
    color += gold * glyph * ring * 0.4;
  }
  float bowl = exp(-radius * (3.0 + 4.0 * (1.0 - well)));
  color += bowl * float3(0.35, 0.12, 0.05) * well;
  color += saturate(hit + low) * bowl * float3(0.15, 0.05, 0.0);
  float centerPres = pf_operator_center_pressure(controls);
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(0.85, 0.55, 0.08), float3(0.42, 0.08, 0.55));
  color = pf_depth_structure(color, p, time, pigment, spin * 0.3);
  return float4(saturate(color), 1.0);
}
