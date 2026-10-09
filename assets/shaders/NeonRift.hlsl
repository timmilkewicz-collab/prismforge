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
  float shear = lerp(-0.8, 0.8, controls.x);
  float width = lerp(0.012, 0.12, saturate(controls.y + assist * 0.2 * musicalB.y));
  float charge = lerp(0.2, 1.4, saturate(controls.z + assist * 0.22 * musicalB.x));
  float split = lerp(0.0, 0.45, saturate(controls.w + assist * 0.16 * (musicalA.z - 0.35)));
  float low = knee(bass);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float centerPres = pf_operator_center_pressure(controls);
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, centerPres, c0, c1, c2, c3);
  float2 pw = pf_multi_center_field(p, time, centerPres, 0.9);
  float rift = pw.x - shear * pw.y + 0.08 * sin(pw.y * 9.0 + time * 0.4) + split * sin(time * 0.2);
  rift += centerPres * 0.12 * sin(atan2(p.y - c0.y, p.x - c0.x) * 3.0 + time * 0.5);
  rift -= centerPres * 0.10 * cos(atan2(p.y - c2.y, p.x - c2.x) * 2.0 - time * 0.4);
  float gap = abs(rift);
  float3 color = float3(0.01, 0.0, 0.02);
  float plate = smoothstep(width, width + 0.04, gap);
  float2 left = p - float2(rift * 0.35 + split, 0.0);
  float2 right = p + float2(rift * 0.35 + split, 0.0);
  float strataL = abs(sin(left.y * 18.0 + left.x * 4.0 - time * 0.2));
  float strataR = abs(sin(right.y * 14.0 - right.x * 6.0 + time * 0.17));
  color += plate * float3(0.12, 0.0, 0.08) * (0.3 + strataL);
  color += plate * float3(0.0, 0.05, 0.12) * strataR * step(0.0, rift);
  float bolt = pow(saturate(0.5 + 0.5 * sin(p.y * 40.0 + time * (3.0 + 4.0 * charge) + low * 6.0)), 18.0);
  bolt *= 1.0 - smoothstep(0.0, width * (1.4 + charge), gap);
  float fork = pow(saturate(0.5 + 0.5 * sin(p.y * 23.0 - p.x * 8.0 + time * 5.0)), 22.0);
  fork *= bolt;
  color += bolt * float3(1.0, 0.15, 0.55) * charge;
  color += fork * float3(0.2, 0.75, 1.0) * 0.6;
  float lip = 1.0 - smoothstep(width * 0.2, width, gap);
  color += lip * float3(0.9, 0.2, 0.45) * (0.4 + 0.6 * saturate(hit));
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(1.0, 0.2, 0.55), float3(0.25, 0.75, 1.0));
  color = pf_depth_structure(color, p, time, charge, width * 2.0);
  return float4(saturate(color), 1.0);
}
