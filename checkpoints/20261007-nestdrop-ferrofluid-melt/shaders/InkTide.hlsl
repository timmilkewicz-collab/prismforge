cbuffer SceneInputs : register(b0) {
  float time;
  float bass;
  float mids;
  float highs;
  float hit;
  float2 resolution;
  float2 pad;
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

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float drift = time * (0.10 + 0.06 * saturate(mids));
  float current = noise2(p * 3.2 + float2(drift, -drift * 0.6));
  float2 w = p + 0.15 * float2(
      sin(p.y * 7.0 + current * 5.0 + drift * 3.0),
      cos(p.x * 6.0 - current * 4.0 - drift * 2.0));
  float marbling = sin(w.x * 11.0 + sin(w.y * 8.0 - drift) * 2.6 + current * 5.0);
  float ink = smoothstep(-0.42, 0.36, marbling);
  float edge = 1.0 - smoothstep(0.025, 0.13, abs(marbling - 0.24));
  float droplets = noise2(w * 15.0 - drift * 0.5);
  droplets = smoothstep(0.78, 0.95, droplets) * ink;

  float3 abyss = float3(0.008, 0.018, 0.055);
  float3 indigo = float3(0.07, 0.05, 0.26);
  float3 teal = float3(0.01, 0.54, 0.57);
  float3 color = lerp(abyss, indigo, ink);
  color = lerp(color, teal, saturate(edge * (0.53 + 0.30 * bass)));
  color += droplets * float3(0.34, 0.80, 0.88);
  color += edge * saturate(hit) * float3(0.10, 0.15, 0.20);
  color *= 0.88 + 0.12 * sin(w.y * 5.0 + drift);
  return float4(saturate(color), 1.0);
}
