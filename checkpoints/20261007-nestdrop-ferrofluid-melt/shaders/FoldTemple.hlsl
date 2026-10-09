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

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float2 translated = p * float2(8.0, 10.0);
  translated.x += 0.28 * sin(translated.y * 0.31 + time * 0.17);
  float2 tile = floor(translated);
  float2 f = frac(translated);
  float diagonal = f.x + f.y - 1.0;
  float facet = step(0.0, diagonal);
  float paperFold = abs(diagonal);
  float edge = min(min(f.x, f.y), min(1.0 - f.x, 1.0 - f.y));
  float seam = 1.0 - smoothstep(0.012, 0.04, min(edge, paperFold * 0.7071));
  float variation = hash21(tile);
  float light = 0.36 + 0.26 * facet + 0.17 * variation;
  light += 0.14 * sin(time * 0.21 + tile.x * 0.27 + tile.y * 0.19);
  float3 parchment = lerp(float3(0.16, 0.25, 0.26),
                           float3(0.82, 0.61, 0.34),
                           saturate(light + bass * 0.11));
  float3 color = parchment * (0.72 + 0.28 * abs(diagonal));
  color = lerp(color, float3(0.052, 0.085, 0.10), seam * 0.75);

  // A central doorway holds the tiled paper architecture together.
  float arch = max(abs(p.x) - 0.22,
                   length(float2(p.x, max(p.y - 0.13, 0.0))) - 0.25);
  float door = 1.0 - smoothstep(-0.01, 0.01, arch);
  color = lerp(color, float3(0.018, 0.035, 0.055), door * 0.68);
  float rim = 1.0 - smoothstep(0.0, 0.012, abs(arch));
  color += rim * float3(0.19, 0.21, 0.13) * (0.5 + mids * 0.5);
  color *= 1.0 + 0.05 * hit;
  return float4(saturate(color), 1.0);
}
