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

float boxDistance(float2 p, float2 halfSize) {
  float2 d = abs(p) - halfSize;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  p.x += 0.025 * sin(time * 0.18);
  float3 color = float3(0.007, 0.010, 0.026);

  // Eight moving, nested frames suggest an endless faceted atrium.
  [unroll] for (int i = 0; i < 8; ++i) {
    float depth = frac((float)i / 8.0 + time * (0.055 + 0.012 * bass));
    float size = 0.055 + depth * depth * 1.55;
    float2 q = p;
    q.x += 0.025 * sin(time * 0.3 + (float)i * 1.3);
    float d = abs(boxDistance(q, float2(size * 1.55, size)));
    float pixel = 1.5 / max(resolution.y, 1.0);
    float frameLine = 1.0 - smoothstep(pixel, pixel + 0.0025 + 0.003 * depth, d);
    float glow = 0.003 / (0.003 + d * d * 75.0);
    float3 tint = lerp(float3(0.06, 0.42, 0.76),
                        float3(0.92, 0.35, 0.77), depth);
    color += tint * (frameLine * 0.23 + glow * 0.065) * (0.45 + depth * 0.8);
  }

  // Floor and ceiling facets converge on the same vanishing point.
  float horizon = abs(p.y);
  float facet = abs(sin((p.x / (horizon + 0.07)) * 1.8));
  float stripe = 1.0 - smoothstep(0.0, 0.06, facet);
  float floorMask = smoothstep(0.13, 0.45, horizon);
  color += stripe * floorMask * float3(0.02, 0.055, 0.08);
  float portal = 1.0 - smoothstep(0.0, 0.13, length(p));
  color += portal * float3(0.025, 0.10, 0.16) * (0.7 + 0.3 * mids);
  color *= 1.0 + 0.12 * saturate(hit);
  return float4(saturate(color), 1.0);
}
