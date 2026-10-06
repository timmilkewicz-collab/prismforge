#ifndef QUALITY_TIER
#define QUALITY_TIER 0
#endif

// Keep the constant-buffer layout identical to SpoutRender::SceneInputs.
cbuffer SceneInputs : register(b0) {
  float time;
  float bass;
  float mids;
  float highs;
  float hit;
  float2 resolution;
  float2 pad;
  float4 sceneParams;
};

struct PSInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

#if QUALITY_TIER == 0
  #define CIRCUIT_OCTAVES 5
#elif QUALITY_TIER == 1
  #define CIRCUIT_OCTAVES 4
#elif QUALITY_TIER == 2
  #define CIRCUIT_OCTAVES 3
#else
  #define CIRCUIT_OCTAVES 2
#endif

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

float audioContour(float value, float floor, float knee) {
  // Room-mic bands commonly live around 0.01-0.10. The knee makes motion
  // clear in that range while keeping silence and loud inputs bounded.
  float signal = max(saturate(value) - floor, 0.0);
  return signal / (signal + knee);
}

float thinRail(float distance, float width) {
  return 1.0 - smoothstep(width, width + max(fwidth(distance), 0.003),
                          distance);
}

// A recursive field of folded circuit paths, liquid data lanes and junctions.
// The line geometry is generated here; no textures, presets or video are used.
float4 main(PSInput input) : SV_TARGET {
  float4 controls = saturate(sceneParams);
  float low = audioContour(bass, 0.004, 0.032);
  float mid = audioContour(mids, 0.004, 0.038);
  float treble = audioContour(highs, 0.003, 0.031);
  float accent = saturate(hit);
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect * 2.0, 2.0);

  // Bass changes the field's scale and fold spacing, not its exposure.
  // Midrange steers its liquid deformation. Both remain visibly different
  // between a quiet mic and ordinary music levels.
  p -= float2(0.025 * sin(time * 0.17),
              0.020 * cos(time * 0.13));
  p = rotate2(p, 0.075 * sin(time * 0.09) + 0.19 * mid);
  p *= (1.28 - 0.23 * controls.z) * (1.0 + 0.30 * low);
  float distanceFromCenter = length(p);
  float coreMask = smoothstep(0.075, 0.28, distanceFromCenter);
  float edgeMask = 1.0 - smoothstep(2.12, 3.10, distanceFromCenter);
  float fieldMask = coreMask * edgeMask;
  float clock = time * (0.18 + 0.42 * controls.y);
  float2 domain = p;
  float3 color = float3(0.0025, 0.0040, 0.0120);

  [unroll] for (int i = 0; i < CIRCUIT_OCTAVES; ++i) {
    float layer = (float)i;
    float depth = (layer + 1.0) / (float)CIRCUIT_OCTAVES;

    // A repeated absolute fold makes the large and small structures related,
    // while an oblique basis prevents a plain orthogonal grid appearance.
    domain = abs(domain + float2(0.020 * mid, -0.025 * low)) -
             float2(0.47 + 0.08 * low, 0.39 - 0.055 * mid);
    domain = float2(domain.x * 0.31 + domain.y * 0.92,
                    domain.y * 0.27 - domain.x * 0.96);
    float scale = exp2(layer * 0.78);
    float2 grid = domain * scale + clock * (0.15 + 0.045 * layer) *
                  float2(1.0, -0.62);
    float fluidity = 0.025 + 0.085 * controls.y + 0.105 * mid;
    grid += fluidity * float2(
        sin(grid.y * 2.2 + clock * (0.9 + 0.15 * layer)),
        sin(grid.x * 2.0 - clock * (0.73 + 0.12 * layer)));

    float2 cellId = floor(grid);
    float2 local = frac(grid) - 0.5;
    float seed = hash21(cellId + layer * 37.17);
    float nodeSeed = hash21(cellId * 1.73 + layer * 13.71);
    float curve = (seed - 0.5) * (0.30 + 0.18 * controls.y);
    float horizontalDistance = abs(local.y -
        curve * local.x * (1.0 - 4.0 * local.x * local.x));
    float verticalDistance = abs(local.x -
        (nodeSeed - 0.5) * 0.38 * local.y *
        (1.0 - 4.0 * local.y * local.y));
    float width = lerp(0.018, 0.011, depth);
    float spine = thinRail(horizontalDistance, width);
    float branch = thinRail(verticalDistance, width * 0.84);

    // The seed is spatially stable; bass grows individual branches instead
    // of changing the brightness of all cells in the same video frame.
    float branchGate = smoothstep(0.59 - 0.27 * controls.x - 0.18 * low,
                                  0.79 - 0.27 * controls.x - 0.18 * low,
                                  seed);
    branch *= branchGate;
    float dataDistance = abs(local.y + 0.20 -
        (seed - 0.5) * 0.11 * local.x *
        (1.0 - 4.0 * local.x * local.x));
    float dataGate = smoothstep(0.59 - 0.21 * controls.x - 0.18 * treble,
                                0.78 - 0.21 * controls.x - 0.18 * treble,
                                nodeSeed);
    float dataRail = thinRail(dataDistance, width * 0.57) * dataGate;
    float2 nodePosition = float2(0.13 * (seed - 0.5), 0.0);
    float nodeDistance = length(local - nodePosition);
    float junction = thinRail(abs(nodeDistance - 0.075), width * 0.70) *
                     branchGate;
    float spark = (1.0 - smoothstep(0.015, 0.055, nodeDistance)) *
                  step(0.71, nodeSeed) * branchGate * accent;

    float finePresence = lerp(1.0, 0.48 + 0.92 * controls.z, depth);
    float attenuation = (0.85 - 0.095 * layer) * fieldMask * finePresence;
    float3 pigment = i % 3 == 0 ? float3(0.025, 0.82, 0.97) :
                     i % 3 == 1 ? float3(0.73, 0.16, 0.98) :
                                  float3(0.99, 0.48, 0.09);
    float charge = 0.42 + 0.88 * controls.w;
    float lumen = spine * 0.35 + branch * 0.51 +
                  dataRail * (0.21 + 0.28 * treble) + junction * 0.44;
    float sheath = 1.0 - smoothstep(0.040, 0.085,
                                    min(horizontalDistance,
                                        verticalDistance));
    color += float3(0.011, 0.018, 0.030) * sheath * attenuation;
    color += pigment * lumen * charge * attenuation;
    // A hit only excites selected tiny junctions, never the whole frame.
    color += float3(1.0, 0.72, 0.25) * spark * 0.44 * attenuation;
  }

  // A deep focal gap stops the intricate field becoming a uniform wallpaper.
  // Smooth tone mapping bounds bright intersections without a beat flash.
  float rim = smoothstep(0.08, 0.13, distanceFromCenter) *
              (1.0 - smoothstep(0.18, 0.31, distanceFromCenter));
  color += rim * float3(0.010, 0.034, 0.050);
  color = color / (0.68 + color);
  return float4(min(color, float3(0.94, 0.94, 0.94)), 1.0);
}
