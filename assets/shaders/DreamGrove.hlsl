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

float hash11(float n) {
  return frac(sin(n * 127.1) * 43758.5453);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float sky = saturate(input.uv.y);
  float3 color = lerp(float3(0.11, 0.045, 0.18),
                      float3(0.31, 0.18, 0.33), sky);
  float moon = 1.0 - smoothstep(0.075, 0.086,
      length(p - float2(0.42, 0.29)));
  color += moon * float3(0.49, 0.40, 0.29);
  float mist = 1.0 - smoothstep(0.05, 0.27, abs(p.y + 0.10));
  color += mist * float3(0.065, 0.055, 0.083);

  // Two parallax ranks of silhouettes, with bounded per-pixel work.
  [unroll] for (int layer = 0; layer < 2; ++layer) {
    [unroll] for (int i = 0; i < 7; ++i) {
      float seed = (float)i + (float)layer * 17.0;
      float spacing = aspect / 6.0;
      float x = ((float)i - 3.0) * spacing +
                (hash11(seed) - 0.5) * spacing * 0.50;
      float baseY = -0.45 - (float)layer * 0.10;
      float height = 0.24 + 0.12 * hash11(seed + 4.0) +
                     (float)layer * 0.12;
      float sway = 0.015 * sin(time * 0.25 + seed) * (0.4 + mids);
      float treeX = x + sway * saturate((p.y - baseY) / height);
      float trunk = 1.0 - smoothstep(0.005, 0.013 + layer * 0.004,
                                   abs(p.x - treeX));
      trunk *= step(baseY, p.y) * step(p.y, baseY + height);
      float2 canopyP = (p - float2(x + sway, baseY + height)) /
                       float2(0.085 + layer * 0.035, 0.085 + layer * 0.03);
      float canopy = 1.0 - smoothstep(0.78, 1.02, length(canopyP));
      float leaf = 0.86 + 0.14 * sin(canopyP.x * 9.0 +
                                     canopyP.y * 11.0 + seed);
      float silhouette = saturate(trunk + canopy * leaf);
      float3 foliage = (layer == 0) ? float3(0.052, 0.11, 0.16)
                                    : float3(0.018, 0.045, 0.078);
      color = lerp(color, foliage, silhouette);
    }
  }

  float ground = 1.0 - smoothstep(-0.47, -0.34, p.y);
  color = lerp(color, float3(0.015, 0.035, 0.061), ground);
  color += mist * float3(0.025, 0.018, 0.032) * (0.5 + bass * 0.3);
  color *= 1.0 + 0.04 * hit;
  return float4(saturate(color), 1.0);
}
