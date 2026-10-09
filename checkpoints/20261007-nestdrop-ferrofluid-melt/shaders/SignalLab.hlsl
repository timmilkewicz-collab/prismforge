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
  float2 gridUV = uv * float2(24.0, 14.0);
  float2 cellEdge = min(frac(gridUV), 1.0 - frac(gridUV));
  float minorGrid = 1.0 - smoothstep(0.0, 0.035, min(cellEdge.x, cellEdge.y));
  float2 majorUV = uv * float2(6.0, 3.5);
  float2 majorEdge = min(frac(majorUV), 1.0 - frac(majorUV));
  float majorGrid = 1.0 - smoothstep(0.0, 0.013, min(majorEdge.x, majorEdge.y));
  float3 color = float3(0.005, 0.023, 0.024);
  color += minorGrid * float3(0.006, 0.055, 0.052);
  color += majorGrid * float3(0.015, 0.085, 0.074);

  float x = uv.x;
  float sweep = time * 2.1;
  float wave = 0.50 + (0.12 + 0.07 * bass) * sin(x * 33.0 - sweep);
  wave += 0.045 * sin(x * 71.0 + sweep * 1.77) * (0.3 + highs);
  float secondary = 0.51 + 0.085 * sin(x * 12.0 + sweep * 0.49 + 1.3) *
      (0.35 + mids * 0.55);
  float d1 = abs(uv.y - wave);
  float d2 = abs(uv.y - secondary);
  float trace = 1.0 - smoothstep(0.001, 0.006, d1);
  float subtrace = 1.0 - smoothstep(0.001, 0.004, d2);
  color += float3(0.09, 0.91, 0.59) * trace;
  color += float3(0.16, 0.52, 0.85) * subtrace * 0.55;
  color += float3(0.01, 0.18, 0.09) * (0.009 / (d1 + 0.009));

  float column = floor(x * 32.0);
  float barHeight = 0.025 + 0.12 * saturate(
      0.30 * bass + 0.30 * mids + 0.40 * highs +
      0.24 * sin(column * 1.81 + time * 3.2));
  float bar = step(uv.y, barHeight) * step(0.11, frac(x * 32.0));
  color += bar * float3(0.025, 0.26, 0.20);
  float cursor = 1.0 - smoothstep(0.0, 0.004, abs(x - frac(time * 0.11)));
  color += cursor * float3(0.04, 0.25, 0.16) * (0.5 + hit);
  return float4(saturate(color), 1.0);
}
