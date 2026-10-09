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
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float3 color = float3(0.020, 0.008, 0.036);
  float atmosphere = 1.0 - smoothstep(0.04, 0.85, length(p));
  color += atmosphere * float3(0.045, 0.013, 0.055);

  // Five slowly dividing radial colonies. All petals are analytic; no feedback
  // texture or previous frame is required.
  [unroll] for (int i = 0; i < 5; ++i) {
    float seed = (float)i;
    float2 center = float2(
        0.43 * sin(seed * 2.399 + time * 0.15),
        0.27 * cos(seed * 1.917 - time * 0.11));
    float2 d = p - center;
    float angle = atan2(d.y, d.x);
    float radius = length(d);
    float flowerRadius = 0.075 + 0.015 * sin(angle * 7.0 +
        time * (0.4 + 0.05 * seed) + seed * 1.8);
    flowerRadius *= 0.88 + 0.32 * bass;
    float petal = 1.0 - smoothstep(flowerRadius - 0.012,
                                  flowerRadius + 0.006, radius);
    float membrane = 1.0 - smoothstep(0.004, 0.017,
                                      abs(radius - flowerRadius));
    float nucleus = 1.0 - smoothstep(0.006, 0.024, radius);
    float halo = 0.006 / (0.006 + radius * radius * 38.0);
    float3 pigment = lerp(float3(0.98, 0.24, 0.48),
                          float3(0.20, 0.83, 0.67), frac(seed * 0.37));
    color += pigment * (petal * 0.32 + membrane * 0.26 + halo * 0.055);
    color += nucleus * float3(0.52, 0.36, 0.28) * (0.5 + hit);
  }

  float substrate = sin(p.x * 11.0 + time * 0.23) *
                    sin(p.y * 15.0 - time * 0.17);
  color += substrate * float3(0.010, 0.003, 0.014);
  return float4(saturate(color), 1.0);
}
