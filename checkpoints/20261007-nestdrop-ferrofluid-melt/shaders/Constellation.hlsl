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

float segmentDistance(float2 p, float2 a, float2 b) {
  float2 v = b - a;
  float t = saturate(dot(p - a, v) / max(dot(v, v), 0.0001));
  return length(p - (a + v * t));
}

float2 nodePosition(float seed, float t) {
  return float2(0.53 * sin(seed * 2.399 + t * 0.12),
                0.34 * sin(seed * 1.73 - t * 0.08));
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float3 color = float3(0.004, 0.008, 0.032);

  float2 starCell = floor(input.uv * float2(65.0, 37.0));
  float2 starLocal = frac(input.uv * float2(65.0, 37.0));
  float2 starCenter = float2(hash21(starCell),
                             hash21(starCell + 17.13));
  float starSeed = hash21(starCell + 39.71);
  float starDistance = length((starLocal - starCenter) *
                              float2(aspect, 1.0));
  float star = (1.0 - smoothstep(0.025, 0.13, starDistance)) *
               step(0.72, starSeed);
  color += star * float3(0.18, 0.31, 0.46) * (0.5 + highs * 0.5);

  [unroll] for (int i = 0; i < 8; ++i) {
    float seed = (float)i;
    float2 a = nodePosition(seed, time);
    float2 b = nodePosition(fmod(seed + 3.0, 8.0), time);
    float lineDistance = segmentDistance(p, a, b);
    float link = 1.0 - smoothstep(0.001, 0.0035, lineDistance);
    color += link * float3(0.028, 0.105, 0.16);
    float distanceToNode = length(p - a);
    float core = 1.0 - smoothstep(0.003, 0.012, distanceToNode);
    float aura = 0.003 / (0.003 + distanceToNode * distanceToNode * 22.0);
    color += (core * 0.60 + aura * 0.045) *
             lerp(float3(0.49, 0.80, 1.0), float3(0.95, 0.58, 0.78),
                  frac(seed * 0.37)) * (0.8 + 0.2 * bass);
  }

  color *= 1.0 + 0.05 * hit;
  return float4(saturate(color), 1.0);
}
