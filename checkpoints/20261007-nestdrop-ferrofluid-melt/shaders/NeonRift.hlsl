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

float segmentDistance(float2 p, float2 a, float2 b) {
  float2 v = b - a;
  float t = saturate(dot(p - a, v) / max(dot(v, v), 0.0001));
  return length(p - (a + v * t));
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float shift = 0.034 * sin(p.y * 14.0 + time * 0.9) +
      0.018 * sin(p.y * 31.0 - time * 1.3);
  float distanceToRift = abs(p.x - shift);
  float side = step(shift, p.x);
  float3 color = lerp(float3(0.022, 0.004, 0.071),
                        float3(0.005, 0.023, 0.066), side);
  float fissure = 1.0 - smoothstep(0.0, 0.018 + 0.009 * bass, distanceToRift);
  float aura = 0.018 / (distanceToRift + 0.018);
  color += aura * float3(0.19, 0.018, 0.42) * (0.5 + bass * 0.7);
  color += fissure * float3(0.82, 0.30, 0.95);

  [unroll] for (int i = 0; i < 5; ++i) {
    float seed = (float)i;
    float y = -0.39 + seed * 0.20 + 0.025 * sin(time * 0.8 + seed);
    float x0 = 0.034 * sin(y * 14.0 + time * 0.9) +
        0.018 * sin(y * 31.0 - time * 1.3);
    float direction = (fmod(seed, 2.0) < 1.0) ? -1.0 : 1.0;
    float2 a = float2(x0, y);
    float2 b = a + float2(direction * (0.18 + 0.05 * sin(time + seed)),
                           0.09 * sin(time * 0.4 + seed * 2.0));
    float d = segmentDistance(p, a, b);
    float beam = 1.0 - smoothstep(0.001, 0.006, d);
    float halo = 0.002 / (d + 0.002);
    color += (beam * 0.36 + halo * 0.09) *
        lerp(float3(0.22, 0.47, 0.96), float3(0.84, 0.11, 0.62),
             frac(seed * 0.37));
  }

  float2 shifted = p + float2(-shift, 0.0);
  float pulse = sin(length(shifted) * 32.0 - time * (2.0 + bass));
  color += (0.5 + 0.5 * pulse) * float3(0.009, 0.015, 0.036);
  color *= 1.0 + 0.10 * saturate(hit);
  return float4(saturate(color), 1.0);
}
