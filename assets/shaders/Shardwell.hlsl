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

float2 rotate2(float2 p, float angle) {
  float sn;
  float cs;
  sincos(angle, sn, cs);
  return float2(cs * p.x - sn * p.y, sn * p.x + cs * p.y);
}

float hash21(float2 p) {
  float3 q = frac(float3(p.x, p.y, p.x) *
                  float3(0.1031, 0.1030, 0.0973));
  q += dot(q, q.yzx + 33.33);
  return frac((q.x + q.y) * q.z);
}

float3 shardPigment(float index) {
  if (index < 0.24) return float3(0.035, 0.77, 0.96);
  if (index < 0.48) return float3(0.98, 0.19, 0.11);
  if (index < 0.73) return float3(1.00, 0.76, 0.045);
  return float3(0.62, 0.14, 0.98);
}

float audioContour(float value, float floor, float knee) {
  // A bounded knee makes 0.01-0.10 room-mic bands move geometry while
  // preserving the exact zero-audio shape and avoiding threshold pops.
  float signal = max(saturate(value) - floor, 0.0);
  return signal / (signal + knee);
}

// One screen-space cell describes a differently oriented three-face block.
// Differently scaled fields provide depth without a per-pixel raymarch.
// Spatial cell IDs are stable; all movement and lighting evolve continuously.
float4 debrisLayer(float2 p, float clock, float density, float orbit,
                   float seed, float softness, float brightness,
                   float visibleFrom, float aperture, float mid, float treble,
                   float accent) {
  float2 g = rotate2(p, clock * orbit + seed * 0.37) * density;
  g += float2(clock * orbit * 1.12, -clock * orbit * 0.73);
  float2 id = floor(g);
  float2 q = frac(g) - 0.5;

  float shapeSeed = hash21(id + seed * 13.17);
  float orientSeed = hash21(id * 1.37 + seed * 4.91);
  float colorSeed = hash21(id * 0.79 + seed * 17.29);
  float placementSeed = hash21(id * 1.83 + seed * 2.43);
  q -= (float2(placementSeed, shapeSeed) - 0.5) * 0.075;
  q = rotate2(q, clock * (0.17 + 0.32 * orientSeed) + 0.11 * mid +
                    orientSeed * 6.2831853);

  float halfSize = (0.267 + 0.060 * shapeSeed) * (1.0 + 0.11 * mid);
  float2 aq = abs(q);
  // A projected cube has six outer edges and three large planar faces. This
  // polygon stays angular even when the nearest layer is softly defocused.
  float d = max(aq.x / 0.91,
                aq.y + 0.67 * aq.x) - halfSize;
  float aa = max(fwidth(d) * 0.85, softness);
  float block = 1.0 - smoothstep(-aa, aa, d);
  float bevel = block * (1.0 - smoothstep(0.003, 0.046, -d));
  float faceBoundary = q.y - 0.445 * aq.x;
  float topFace = step(0.0, faceBoundary);
  float leftFace = (1.0 - topFace) * step(q.x, 0.0);
  float rightFace = (1.0 - topFace) * step(0.0, q.x);
  float faceChoice = frac(orientSeed * 1.91 + shapeSeed * 0.67);
  float litFace = topFace * (1.0 - step(0.40, faceChoice)) +
                  leftFace * step(0.40, faceChoice) *
                             (1.0 - step(0.70, faceChoice)) +
                  rightFace * step(0.70, faceChoice);
  float emissive = smoothstep(0.49, 0.76, colorSeed);
  float3 pigment = shardPigment(frac(colorSeed * 1.19 + orientSeed * 0.35));
  float3 darkFace = topFace * float3(0.091, 0.096, 0.105) +
                    leftFace * float3(0.019, 0.023, 0.034) +
                    rightFace * float3(0.042, 0.049, 0.059);
  float facetRidge = 1.0 - smoothstep(0.003, 0.020,
                                     abs(faceBoundary));
  facetRidge += (1.0 - topFace) *
                (1.0 - smoothstep(0.003, 0.020, aq.x));
  float3 material = darkFace + pigment * emissive *
                    (0.12 + 0.90 * litFace +
                     bevel * (0.23 + 0.10 * treble)) *
                    (1.0 + 0.13 * accent);
  material += facetRidge * block *
              (float3(0.025, 0.034, 0.047) +
               pigment * emissive * (0.23 + 0.20 * treble));

  // Most cells remain black. Glow belongs to the chosen bright faces, not to
  // the whole frame, and the large foreground layer has a soft focal edge.
  float occupancy = step(visibleFrom, placementSeed);
  float halo = (1.0 - smoothstep(0.0, 0.16 + softness, d)) *
               emissive * 0.18;
  float r = length(p);
  float wellGate = smoothstep(aperture - 0.016, aperture + 0.100, r);
  float rimFalloff = 1.0 - smoothstep(0.79, 1.17, r);
  float distribution = wellGate * rimFalloff * occupancy * brightness;
  float coverage = block * distribution;
  return float4(material * coverage + pigment * halo * distribution,
                coverage);
}

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float low = audioContour(bass, 0.004, 0.043);
  float mid = audioContour(mids, 0.004, 0.038);
  float treble = audioContour(highs, 0.003, 0.030);
  float accent = saturate(hit);

  // A dark, slightly off-center opening remains visible even during a bass
  // hit. Bass changes its width, while the camera drifts on a slower path.
  p -= float2(0.065 + 0.014 * sin(time * 0.19),
             -0.019 + 0.011 * cos(time * 0.17));
  float aperture = 0.145 + 0.052 * low;
  float r = length(p);
  float wellRim = smoothstep(aperture - 0.018, aperture + 0.006, r) *
                  (1.0 - smoothstep(aperture + 0.025,
                                    aperture + 0.094, r));
  float3 color = float3(0.0015, 0.0020, 0.0045);
  color += wellRim * float3(0.006, 0.019, 0.028);

  float4 layer = debrisLayer(p, time, 20.0, 0.052, 1.0, 0.005, 0.55,
                             0.39, aperture, mid, treble, accent);
  color = color * (1.0 - layer.a) + layer.rgb;

#if QUALITY_TIER <= 1
  layer = debrisLayer(p, time, 12.0, -0.075, 2.0, 0.006, 0.88,
                      0.44, aperture + 0.020, mid, treble, accent);
  color = color * (1.0 - layer.a) + layer.rgb;
#endif

  layer = debrisLayer(p, time, 6.3, 0.099, 3.0, 0.009, 1.0,
                      0.47, aperture + 0.046, mid, treble, accent);
  color = color * (1.0 - layer.a) + layer.rgb;

#if QUALITY_TIER == 0
  // Deliberately large soft blocks float in front of the crisp swarm.
  // Removing this field is the first quality reduction under load.
  layer = debrisLayer(p, time, 2.9, -0.122, 4.0, 0.032, 0.86,
                      0.61, aperture + 0.19, mid, treble, accent);
  color = color * (1.0 - layer.a) + layer.rgb;
#endif

  // Compress isolated luminous facets without lifting the deep-black well.
  color = color / (0.76 + color);
  return float4(min(color, float3(0.92, 0.92, 0.92)), 1.0);
}
