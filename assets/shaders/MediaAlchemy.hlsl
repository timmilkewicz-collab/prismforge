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

float4 main(PSInput input) : SV_TARGET {
  float2 uv = input.uv;
  float2 tile = floor(uv * 2.0);
  float2 q = frac(uv * 2.0) - 0.5;
  float panel = tile.x + tile.y * 2.0;
  float3 color;

  // Four synthetic feeds form a collage even when no media input is attached.
  if (panel < 0.5) {
    float horizon = 0.08 * sin(q.x * 13.0 + time * 0.35) +
        0.035 * sin(q.x * 31.0 - time * 0.5);
    float land = smoothstep(horizon - 0.025, horizon + 0.025, q.y);
    color = lerp(float3(0.03, 0.12, 0.18),
                 float3(0.77, 0.29, 0.18), land);
    color += 0.12 * (0.5 + 0.5 * sin(q.x * 19.0 + q.y * 14.0 + time));
  } else if (panel < 1.5) {
    float2 spun = q + 0.06 * float2(sin(time * 0.28), cos(time * 0.31));
    float r = length(spun);
    float ring = 1.0 - smoothstep(0.006, 0.025,
        abs(frac(r * 8.0 - time * 0.18) - 0.5));
    color = float3(0.025, 0.035, 0.13) +
        ring * float3(0.17, 0.72, 0.83) * (0.5 + highs);
  } else if (panel < 2.5) {
    float strips = step(0.55, frac(q.y * 17.0 + time * 0.6));
    float blocks = step(0.43, frac(q.x * 9.0 + floor(q.y * 17.0) * 0.31));
    color = lerp(float3(0.05, 0.04, 0.14),
                 float3(0.78, 0.51, 0.08), strips * blocks);
    color += float3(0.08, 0.03, 0.11) * (0.5 + mids);
  } else {
    float folded = sin(q.x * 20.0 + 4.0 * sin(q.y * 8.0 + time * 0.5));
    float veil = smoothstep(-0.35, 0.45, folded);
    color = lerp(float3(0.02, 0.10, 0.07),
                 float3(0.66, 0.17, 0.41), veil);
    color += float3(0.04, 0.10, 0.06) * bass;
  }

  float border = 1.0 - smoothstep(0.47, 0.49, max(abs(q.x), abs(q.y)));
  color *= border;
  float scan = 0.95 + 0.05 * sin(uv.y * resolution.y * 0.7);
  color *= scan;
  float wipe = 1.0 - smoothstep(0.0, 0.014,
      abs(uv.x - frac(time * 0.075)));
  color += wipe * float3(0.09, 0.12, 0.14) * (0.3 + hit);
  return float4(saturate(color), 1.0);
}
