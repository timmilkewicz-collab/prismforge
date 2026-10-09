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
  float cohesion = lerp(0.25, 1.4, saturate(controls.x + assist * 0.14 * (musicalA.z - 0.4)));
  float spread = lerp(0.35, 1.15, controls.y);
  float trailAmt = lerp(0.2, 1.0, controls.z);
  float spec = lerp(0.4, 1.3, saturate(controls.w + assist * 0.1 * musicalB.x));
  float low = knee(bass);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float centerPres = pf_operator_center_pressure(controls);
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, centerPres, c0, c1, c2, c3);
  float2 roost = lerp(float2(0.22 * sin(time * 0.19), 0.12 * cos(time * 0.15)), c0, centerPres);
  float3 color = float3(0.004, 0.008, 0.016);

#if QUALITY_TIER >= 3
  int count = 7;
#elif QUALITY_TIER == 2
  int count = 9;
#else
  int count = 12;
#endif
  [loop] for (int i = 0; i < count; ++i) {
    float seed = (float)i;
    float speed = time * (0.28 + 0.04 * frac(seed * 0.37)) * lerp(0.6, 1.4, controls.x);
    float orbit = (0.15 + 0.05 * frac(seed * 0.61)) * spread;
    float2 center = roost * cohesion + float2(
        orbit * sin(speed + seed * 2.4),
        orbit * 0.72 * cos(speed * 1.17 + seed * 1.7));
    float2 neighbor = roost * cohesion + float2(
        orbit * sin(speed + (seed + 1.0) * 2.4),
        orbit * 0.72 * cos(speed * 1.17 + (seed + 1.0) * 1.7));
    float2 velocity = float2(cos(speed + seed * 2.4), 0.72 * 1.17 * cos(speed * 1.17 + seed));
    float radius = 0.018 + 0.012 * frac(seed * 0.618) + 0.008 * low;
    float dist = length(p - center);
    float body = 1.0 - smoothstep(radius * 0.72, radius, dist);
    float2 delta = (p - center) / max(radius, 0.0001);
    float z = sqrt(saturate(1.0 - dot(delta, delta)));
    float3 normal = normalize(float3(delta, z + 0.05));
    float highlight = pow(saturate(dot(normal, normalize(float3(-0.35, 0.55, 0.85)))), 16.0);
    float3 chrome = float3(0.16, 0.28, 0.36) + highlight * float3(0.62, 0.74, 0.82) * spec;
    float link = 1.0 - smoothstep(0.0015, 0.006, segmentDistance(p, center, neighbor));
    link *= smoothstep(0.22, 0.05, length(center - neighbor));
    float2 tail = center - normalize(velocity + 0.001) * (0.08 + 0.1 * trailAmt);
    float trail = 1.0 - smoothstep(0.001, 0.008, segmentDistance(p, tail, center));
    color += link * float3(0.25, 0.55, 0.62) * 0.35;
    color += trail * float3(0.05, 0.16, 0.18) * trailAmt;
    color = lerp(color, chrome, body);
  }
  color += saturate(hit) * float3(0.02, 0.03, 0.04);
  color += exp(-length(p - c1) * 14.0) * float3(0.35, 0.55, 0.65) * centerPres * spec;
  color += exp(-length(p - c2) * 13.0) * float3(0.45, 0.62, 0.72) * centerPres * 0.8;
  color = pf_center_rim(color, p, time, centerPres, float3(0.2, 0.45, 0.55), float3(0.55, 0.7, 0.85));
  color = pf_depth_structure(color, p, time, spread, cohesion * 0.35);
  return float4(saturate(color), 1.0);
}
