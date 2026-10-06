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
  // Render-thread envelopes: music energy, decaying onset, integrated flow,
  // and an eased three-palette / topology phrase position.
  float4 reactive;
  float4 sceneParams;
  // Appended ABI: renderer-owned MusicalStateFrame interpretation.
  // A = flow, density, topology, palette; B = impact, release, variation,
  // enabled. The legacy prefix above remains byte-for-byte unchanged.
  float4 musicalA;
  float4 musicalB;
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
  bool useMusical = musicalB.w > 0.5;
  float music;
  float pulse;
  float flow;
  float palettePhase;
  float density = 0.0;
  float topologyDrive = 0.0;
  float releaseTail = 0.0;
  float variation = 0.0;
  float low;
  float mid;
  float treble;
  if (useMusical) {
    // These visual roles come only from the renderer's scene adapter. Raw FFT
    // bands and one-frame hit flags cannot define high-level behavior here.
    density = saturate(musicalA.y);
    topologyDrive = saturate(musicalA.z);
    releaseTail = saturate(musicalB.y);
    variation = saturate(musicalB.z);
    music = saturate(max(density, 0.38 * releaseTail));
    pulse = saturate(musicalB.x);
    flow = musicalA.x;
    palettePhase = saturate(musicalA.w) * 3.0;
    low = saturate(0.72 * density + 0.28 * releaseTail);
    mid = saturate(0.66 * topologyDrive + 0.22 * density +
                   0.12 * variation);
    treble = saturate(0.58 * density + 0.34 * pulse +
                      0.08 * variation);
  } else {
    music = saturate(reactive.x);
    pulse = saturate(reactive.y);
    flow = reactive.z;
    palettePhase = saturate(reactive.w) * 3.0;
    // The grouped FFT values can be far smaller than room-mic RMS. The broad
    // music envelope carries the field while each band still has a distinct job.
    low = saturate(audioContour(bass, 0.0008, 0.012) + 0.25 * music);
    mid = saturate(audioContour(mids, 0.0008, 0.014) + 0.20 * music);
    treble = saturate(audioContour(highs, 0.0006, 0.012) + 0.13 * music);
  }
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect * 2.0, 2.0);

  // A continuously integrated music clock drives advection without a jump
  // when the volume changes. Two scales of opposing curl make the entire
  // topology flow, instead of translating an otherwise static wallpaper.
  float2 broadCurl = float2(
      sin(p.y * 2.35 + flow * 1.31) +
          0.42 * sin(p.x * 4.1 - flow * 0.81),
      cos(p.x * 2.58 - flow * 1.13) +
          0.42 * sin(p.y * 3.7 + flow * 0.94));
  float2 orbit = float2(-p.y, p.x) / (1.0 + dot(p, p));
  p += broadCurl * (0.035 + 0.17 * music + 0.055 * mid);
  p += orbit * (0.045 + 0.105 * low) * sin(flow * 0.58 + length(p));
  p = rotate2(p, 0.06 * sin(flow * 0.31) + 0.24 * mid);
  p *= (1.24 - 0.23 * controls.z) * (1.0 + 0.22 * low) *
       (useMusical ? 1.0 - 0.065 * pulse : 1.0);
  float distanceFromCenter = length(p);
  float coreMask = smoothstep(0.075, 0.28, distanceFromCenter);
  float edgeMask = 1.0 - smoothstep(2.12, 3.10, distanceFromCenter);
  float fieldMask = coreMask * edgeMask;
  float clock = flow * (0.34 + 0.59 * controls.y);
  float2 domain = p;
  float3 color = float3(0.0025, 0.0040, 0.0120);
  float palettePosition = frac(palettePhase / 3.0) * 3.0;
  float paletteStep = floor(palettePosition);
  float paletteBlend = smoothstep(0.0, 1.0, frac(palettePosition));
  // Each phrase smoothly swaps both shape language and a full color family.
  // The three-state loop has identical endpoints, so it never hard-cuts.
  float topologyMode = useMusical ? topologyDrive :
      0.5 - 0.5 * cos(palettePhase * 3.14159265);

  [unroll] for (int i = 0; i < CIRCUIT_OCTAVES; ++i) {
    float layer = (float)i;
    float depth = (layer + 1.0) / (float)CIRCUIT_OCTAVES;

    // A repeated absolute fold makes the large and small structures related,
    // while an oblique basis prevents a plain orthogonal grid appearance.
    domain = abs(domain + float2(0.028 * mid, -0.035 * low)) -
             float2(0.47 + 0.10 * low, 0.39 - 0.075 * mid);
    domain = float2(domain.x * 0.31 + domain.y * 0.92,
                    domain.y * 0.27 - domain.x * 0.96);
    float scale = exp2(layer * 0.78);
    float2 grid = domain * scale + clock * (0.15 + 0.045 * layer) *
                  float2(1.0, -0.62);
    float fluidity = 0.028 + 0.11 * controls.y +
                     0.10 * mid + 0.065 * music;
    grid += fluidity * float2(
        sin(grid.y * 2.2 + clock * (0.9 + 0.15 * layer)) +
            0.35 * sin(grid.x * 3.4 - flow * 0.8),
        sin(grid.x * 2.0 - clock * (0.73 + 0.12 * layer)) +
            0.35 * cos(grid.y * 3.1 + flow * 0.7));

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
    float branchDrive = useMusical ? density : low;
    float branchGate = smoothstep(0.59 - 0.27 * controls.x - 0.18 * branchDrive,
                                  0.79 - 0.27 * controls.x - 0.18 * branchDrive,
                                  seed);
    branch *= branchGate;
    float dataDistance = abs(local.y + 0.20 -
        (seed - 0.5) * 0.11 * local.x *
        (1.0 - 4.0 * local.x * local.x));
    float dataDrive = useMusical ?
        saturate(0.74 * density + 0.26 * topologyDrive) : treble;
    float dataGate = smoothstep(0.59 - 0.21 * controls.x - 0.18 * dataDrive,
                                0.78 - 0.21 * controls.x - 0.18 * dataDrive,
                                nodeSeed);
    float dataRail = thinRail(dataDistance, width * 0.57) * dataGate;
    float2 nodePosition = float2(0.13 * (seed - 0.5), 0.0);
    float nodeDistance = length(local - nodePosition);
    float junction = thinRail(abs(nodeDistance - 0.075), width * 0.70) *
                     branchGate;
    float spark = (1.0 - smoothstep(0.015, 0.11, nodeDistance)) *
                  step(0.71, nodeSeed) * branchGate * pulse;

    // A sustained onset morphs selected rail cells into curved loops and
    // traveling ribbons. The phrase state changes the whole silhouette over
    // roughly a second; cell seeds stagger the change to avoid a frame cut.
    float loopRadius = 0.22 + 0.055 * low + 0.035 * pulse;
    float loop = thinRail(abs(length(local) - loopRadius), width * 1.2);
    float ribbonDistance = abs(local.y - 0.20 *
        sin(local.x * 5.3 + flow * 1.08 + seed * 6.2831853));
    float ribbon = thinRail(ribbonDistance, width * 1.15);
    float morph = useMusical ? smoothstep(0.12, 0.88,
        topologyMode * 0.72 + pulse * 0.25 + variation * 0.12 +
        (seed - 0.5) * 0.18) : smoothstep(0.12, 0.88,
        topologyMode * 0.66 + pulse * 0.26 + mid * 0.23 +
        (seed - 0.5) * 0.18);

    float finePresence = lerp(1.0, 0.48 + 0.92 * controls.z, depth);
    if (useMusical) finePresence *= lerp(0.42, 1.0, density);
    float attenuation = (0.85 - 0.095 * layer) * fieldMask * finePresence;
    float3 pigment0 = i % 3 == 0 ? float3(0.025, 0.82, 0.97) :
                      i % 3 == 1 ? float3(0.73, 0.16, 0.98) :
                                   float3(0.99, 0.48, 0.09);
    float3 pigment1 = i % 3 == 0 ? float3(0.45, 0.99, 0.06) :
                      i % 3 == 1 ? float3(0.05, 0.40, 1.00) :
                                   float3(1.00, 0.07, 0.35);
    float3 pigment2 = i % 3 == 0 ? float3(1.00, 0.18, 0.07) :
                      i % 3 == 1 ? float3(0.99, 0.79, 0.05) :
                                   float3(0.12, 0.50, 1.00);
    float3 pigment = paletteStep < 1.0 ?
        lerp(pigment0, pigment1, paletteBlend) :
        paletteStep < 2.0 ? lerp(pigment1, pigment2, paletteBlend) :
                            lerp(pigment2, pigment0, paletteBlend);
    float charge = 0.42 + 0.88 * controls.w;
    if (useMusical) charge *= 0.68 + 0.32 * max(density, releaseTail);
    float railLumen = spine * 0.35 + branch * 0.51 +
                      dataRail * (0.21 + 0.28 * treble) + junction * 0.44;
    float loopLumen = loop * (0.43 + 0.10 * low) +
                      ribbon * (0.38 + 0.12 * mid) +
                      junction * 0.19;
    float packet = smoothstep(0.87, 0.99,
        0.5 + 0.5 * cos(local.x * 17.0 - flow * 3.5 + seed * 6.2831853));
    float lumen = lerp(railLumen, loopLumen, morph) +
                   (spine + ribbon) * packet * (0.075 + 0.20 * music);
    if (useMusical) {
      // Release holds a smaller spatial tail after impact instead of snapping
      // immediately back to the calm topology.
      lumen += (spine + 0.65 * ribbon) * releaseTail * 0.10;
    }
    float sheath = 1.0 - smoothstep(0.040, 0.085,
                                    min(horizontalDistance,
                                        verticalDistance));
    color += float3(0.011, 0.018, 0.030) * sheath * attenuation;
    color += pigment * lumen * charge * attenuation;
    // Onsets excite spatially selected joints with a decaying envelope;
    // the background stays dark, avoiding a whole-frame beat flash.
    color += pigment * spark * 0.28 * attenuation;
  }

  // A deep focal gap stops the intricate field becoming a uniform wallpaper.
  // Smooth tone mapping bounds bright intersections without a beat flash.
  float rim = smoothstep(0.08, 0.13, distanceFromCenter) *
              (1.0 - smoothstep(0.18, 0.31, distanceFromCenter));
  color += rim * float3(0.010, 0.034, 0.050);
  color = color / (0.68 + color);
  return float4(min(color, float3(0.94, 0.94, 0.94)), 1.0);
}
