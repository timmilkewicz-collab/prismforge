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
  return length(p - (a + t * v));
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float3 color = float3(0.006, 0.012, 0.023);
  float wash = 1.0 - smoothstep(0.10, 0.78, length(p));
  color += wash * float3(0.015, 0.028, 0.045);

  [unroll] for (int i = 0; i < 12; ++i) {
    float seed = (float)i;
    float speed = time * (0.34 + 0.025 * frac(seed * 0.37));
    float2 center = float2(
        0.53 * sin(speed + seed * 2.399) + 0.08 * sin(time * 0.82 + seed),
        0.31 * sin(speed * 1.27 + seed * 1.71));
    float2 velocity = float2(
        0.53 * cos(speed + seed * 2.399),
        0.31 * 1.27 * cos(speed * 1.27 + seed * 1.71));
    float2 tail = center - normalize(velocity + float2(0.001, 0.001)) * 0.12;
    float radius = 0.022 + 0.009 * frac(seed * 0.618) + 0.007 * bass;
    float2 delta = p - center;
    float dist = length(delta);
    float body = 1.0 - smoothstep(radius - 0.003, radius + 0.002, dist);
    float z = sqrt(saturate(1.0 - dot(delta, delta) / (radius * radius)));
    float3 normal = normalize(float3(delta / radius, z + 0.001));
    float reflection = 0.5 + 0.5 * sin(normal.x * 13.0 + normal.y * 8.0);
    float highlight = pow(saturate(dot(normal, normalize(float3(-0.4, 0.7, 0.9)))), 18.0);
    float3 chrome = float3(0.18, 0.30, 0.39) + reflection * float3(0.34, 0.43, 0.47);
    chrome += highlight * float3(0.55, 0.67, 0.74);
    float trail = 1.0 - smoothstep(0.001, 0.011, segmentDistance(p, tail, center));
    color += trail * float3(0.014, 0.065, 0.075) * (0.4 + highs);
    color = lerp(color, chrome, body);
    color += float3(0.018, 0.042, 0.055) *
        (1.0 - smoothstep(radius, radius * 3.0, dist)) * (0.5 + hit);
  }

  return float4(saturate(color), 1.0);
}
