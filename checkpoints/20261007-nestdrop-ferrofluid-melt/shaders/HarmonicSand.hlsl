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
  float2 plate = p * float2(1.8, 2.7);
  float vibration = 0.08 * sin(time * 0.9 + p.x * 8.0) * (0.3 + bass);
  plate += vibration;

  // Interfering standing waves create changing Chladni-like nodal curves.
  float modeA = sin(plate.x * 5.0) * sin(plate.y * 3.0) -
                sin(plate.x * 3.0) * sin(plate.y * 5.0);
  float modeB = sin(plate.x * 7.0) * sin(plate.y * 4.0) -
                sin(plate.x * 4.0) * sin(plate.y * 7.0);
  float mode = lerp(modeA, modeB, 0.5 + 0.5 * sin(time * 0.13 + mids));
  float node = 1.0 - smoothstep(0.015, 0.13, abs(mode));
  float nearNode = 1.0 - smoothstep(0.04, 0.27, abs(mode));
  float2 grainCell = floor(input.uv * resolution * 0.48);
  float grain = hash21(grainCell);
  float sparkle = step(0.76, grain) * nearNode;

  float3 color = float3(0.045, 0.035, 0.026);
  color += float3(0.18, 0.13, 0.07) * nearNode;
  color += float3(0.66, 0.47, 0.25) * node;
  color += float3(0.25, 0.18, 0.08) * sparkle * (0.4 + highs * 0.6);
  float plateEdge = 1.0 - smoothstep(0.40, 0.62, length(p));
  color *= 0.3 + 0.7 * plateEdge;
  color *= 1.0 + 0.08 * hit;
  return float4(saturate(color), 1.0);
}
