#ifndef QUALITY_TIER
#define QUALITY_TIER 0
#endif

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
  #define VAULT_COUNT 4
#elif QUALITY_TIER == 1
  #define VAULT_COUNT 3
#elif QUALITY_TIER == 2
  #define VAULT_COUNT 2
#else
  #define VAULT_COUNT 2
#endif

float2 rotate2(float2 p, float angle) {
  float s;
  float c;
  sincos(angle, s, c);
  return float2(c * p.x - s * p.y, s * p.x + c * p.y);
}

float2 mirrorWedge(float radius, float angle, float sector, float offset) {
  // The order is continuous: turning its control changes geometry without
  // a frame-wide switch from one integer symmetry count to the next.
  float folded = abs(frac((angle + offset) / sector + 0.5) - 0.5) * sector;
  float s;
  float c;
  sincos(folded, s, c);
  return radius * float2(c, s);
}

float boxDistance(float2 p, float2 halfSize) {
  float2 d = abs(p) - halfSize;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float3 vaultTint(float depth) {
  // A limited two-color material; amber is reserved for a few joints below.
  return lerp(float3(0.025, 0.82, 0.97),
              float3(0.53, 0.18, 0.91),
              smoothstep(0.22, 0.91, depth) * 0.79);
}

float audioContour(float value, float floor, float knee) {
  // Grouped analyzer bands in a live room commonly sit around 0.01-0.10.
  // A rational knee spends most motion there, yet retains a useful response
  // above 0.1; saturation and the positive denominator bound all values.
  float signal = max(saturate(value) - floor, 0.0);
  return signal / (signal + knee);
}

// An original faceted atrium: substantial dark panels, bevels, suspended
// buttresses and selective neon joints. Audio moves surfaces and reveals
// their detail; it never multiplies the whole frame's exposure.
float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 screenP = (input.uv - 0.5) * float2(aspect * 2.0, 2.0);
  float low = audioContour(bass, 0.004, 0.043);
  float mid = audioContour(mids, 0.004, 0.038);
  float treble = audioContour(highs, 0.003, 0.030);
  float accent = saturate(hit);
  float4 controls = saturate(sceneParams);

  // The shell mostly holds its pose; slower drift leaves room for the audio
  // to be the obvious source of panel and perspective movement.
  // The vanishing point is deliberately displaced; the cropped piers below
  // remain attached to the screen, so this is an architectural space rather
  // than a perfectly centered polar mandala.
  float2 p = screenP - float2(0.17, 0.055);
  p = rotate2(p, 0.065 + 0.010 * sin(time * 0.055));
  p += float2(0.012 * sin(time * 0.061),
              0.010 * cos(time * 0.052));
  float radius = length(p);
  float angle = atan2(p.y, p.x);
  float sector = 6.2831853 / (2.0 + 6.0 * controls.x);
  float2 q = mirrorWedge(radius, angle, sector, 0.0);
  float2 staggered = mirrorWedge(radius, angle, sector, sector * 0.5);

  float opening = 0.17 + 0.24 * controls.z + 0.060 * low;
  float apertureMask = smoothstep(opening - 0.012,
                                   opening + 0.026, radius);
  float width = 0.0024 + 0.0085 * controls.w;
  float3 color = float3(0.0010, 0.0014, 0.0030);

  [unroll] for (int i = 0; i < VAULT_COUNT; ++i) {
    float index = (float)i;
    float depth = (index + 0.42) / (float)VAULT_COUNT;
    // The distant panes are tiny; the final two structures crop offscreen.
    // Low frequencies change that perspective instead of scene exposure.
    float z = 0.18 + 2.00 * pow(depth, 2.30);
    z *= 1.0 + 0.28 * low;
    float phase = time * 0.085 + index * 1.13;
    float panelSlide = z * (0.014 * sin(phase) +
                            0.12 * mid);
    float topology = controls.y;
    float3 tint = vaultTint(depth);
    float focus = 0.30 + 0.70 * depth;
    float archPresence = 1.0 - step(0.5, frac(index * 0.5));
    float panelPresence = 1.0 - archPresence;

    // Topology connects a second bay through a bridge as the knob rises.
    // Each layer staggers the active lanes, leaving broad, deliberate black
    // intervals instead of making every mirrored sector equally occupied.
    float lane = abs(frac(angle / sector + 0.5 + index * 0.109) - 0.5);
    float primary = smoothstep(0.027, 0.077, lane) *
                    (1.0 - smoothstep(0.205, 0.270, lane));
    float branch = smoothstep(0.300, 0.345, lane) *
                   (1.0 - smoothstep(0.425, 0.478, lane));
    float bridge = smoothstep(0.215, 0.275, lane) *
                   (1.0 - smoothstep(0.315, 0.370, lane));
    float connected = smoothstep(0.20, 0.86, topology);
    float occupancy = saturate(primary + connected *
                               (0.78 * branch + 0.68 * bridge));

    // The slanted, slightly curved vault profile is much heavier than a
    // wireframe ring. Recessed faces remain almost black between the ribs.
    float profile = q.x + (0.22 + 0.22 * topology) * q.y +
                    0.12 * q.y * q.y / (z + 0.25);
    float span = (0.085 + 0.18 * depth) *
                 (0.87 + 0.27 * topology);
    float surfaceDistance = abs(profile - z);
    float aa = max(fwidth(profile) * 1.1, 0.0017);
    float solid = 1.0 - smoothstep(span - aa, span + aa,
                                   surfaceDistance);
    float outerEdge = 1.0 - smoothstep(width + aa,
                                      width * 2.5 + aa,
                                      abs(surfaceDistance - span));
    float innerJoint = 1.0 - smoothstep(width * 0.7 + aa,
                                       width * 2.2 + aa,
                                       abs(profile - z));
    float shard = saturate(0.30 + 0.52 * q.y / (z + 0.25));
    float3 metal = lerp(float3(0.037, 0.046, 0.061),
                        float3(0.105, 0.118, 0.140), shard);
    metal += tint * innerJoint * 0.072;
    color = lerp(color, metal, solid * occupancy * archPresence *
                 apertureMask * focus);

    // Offset mirrored bays create a second architectural axis. Their body
    // occludes earlier layers; their slim bevels carry the strongest light.
    float2 bayCenter = float2(z * (0.82 + 0.12 * topology),
                              z * (0.12 + 0.14 * topology) + panelSlide);
    float bayScale = lerp(0.54, 1.0, panelPresence);
    float2 baySize = z * bayScale *
                     float2(0.21 + 0.065 * topology,
                            0.13 + 0.075 * (1.0 - topology));
    float2 bayPoint = staggered - bayCenter;
    // A skewed plate, not another neon square. Its outer face is illuminated
    // while the larger interior is visibly filled gunmetal.
    bayPoint.x += bayPoint.y * (0.34 + 0.25 * topology);
    float bayDistance = boxDistance(bayPoint, baySize);
    float bayAa = max(fwidth(bayDistance), 0.0015);
    float bayBody = 1.0 - smoothstep(-bayAa, bayAa,
                                     bayDistance);
    float bayBevel = 1.0 - smoothstep(width + bayAa,
                                      width * 3.0 + bayAa,
                                      abs(bayDistance));
    float bayCut = 1.0 - smoothstep(0.0, baySize.x * 0.92,
                                    abs(bayPoint.x + bayPoint.y * 0.37));
    float faceLight = smoothstep(-0.8, 0.9,
                                 bayPoint.x / max(baySize.x, 0.01));
    float3 bayMetal = lerp(float3(0.039, 0.047, 0.061),
                            float3(0.113, 0.125, 0.149), faceLight);
    bayMetal += tint * 0.014 * bayCut;
    color = lerp(color, bayMetal, bayBody * occupancy *
                 apertureMask * focus);

    // Etched seams become visible at high frequencies only on the plates.
    float engraving = 1.0 - smoothstep(width * 0.65 + bayAa,
                                        width * 2.0 + bayAa,
                                        abs(bayPoint.y +
                                            bayPoint.x * (0.31 + 0.31 * topology)));
    float detail = bayBody * engraving *
                   (0.08 + 0.57 * treble);
    color += tint * detail * occupancy * apertureMask * focus *
             (0.43 + 0.57 * panelPresence);

    float glow = (1.0 - smoothstep(0.0, 0.052 + 0.025 * depth,
                                   abs(surfaceDistance - span))) *
                 (1.0 - solid);
    float edgeFacing = smoothstep(-0.45, 0.55,
                                  bayPoint.x / max(baySize.x, 0.01));
    color += tint * (outerEdge * archPresence *
                     (0.35 + 0.79 * depth) +
                     bayBevel * (0.10 + 0.71 * edgeFacing) *
                     (0.59 + 0.86 * depth) +
                     glow * archPresence * 0.021) *
             occupancy * apertureMask * focus;

    // A hit lights only a small joint on every other bay. No screen-wide
    // threshold or beat-synchronized palette change is present.
    float2 nodePoint = bayPoint - float2(baySize.x * 0.83,
                                         -baySize.y * 0.76);
    float nodeRadius = length(nodePoint);
    float node = 1.0 - smoothstep(width * 2.0,
                                  width * 5.4 + 0.005,
                                  nodeRadius);
    float selected = 1.0 - step(0.5, frac(index * 0.5));
    color += float3(1.0, 0.42, 0.045) * node * selected *
             (0.19 + 0.32 * accent) * occupancy * apertureMask * focus;
  }

  // Cropped foreground piers occlude the more distant mirrored bays. Their
  // broad material remains dark; only a narrow edge and one incised seam
  // read as light. This anchors scale without a sampled image or raymarch.
  float leftPier = -screenP.x -
                   (0.92 + 0.24 * screenP.y +
                    0.09 * abs(screenP.y) - 0.035 * low);
  float rightPier = screenP.x -
                    (1.08 - 0.16 * screenP.y +
                     0.10 * abs(screenP.y) - 0.035 * low);
  float pierDistance = max(leftPier, rightPier);
  float pierAa = max(fwidth(pierDistance), 0.0017);
  float pierBody = smoothstep(-pierAa, pierAa, pierDistance);
  float pierFacet = saturate(0.18 + 0.46 * screenP.y +
                              0.37 * pierDistance);
  float pierDepth = saturate(pierDistance / 0.68);
  float3 pierMetal = lerp(float3(0.051, 0.064, 0.082),
                          float3(0.152, 0.167, 0.187), pierDepth);
  pierMetal += pierFacet * float3(0.022, 0.025, 0.032);
  color = lerp(color, pierMetal, pierBody * 0.98);
  float pierEdge = 1.0 - smoothstep(width + pierAa,
                                    width * 3.2 + pierAa,
                                    abs(pierDistance));
  float seamCoordinate = screenP.x < 0.0 ?
      screenP.y * 0.70 - screenP.x * 0.21 - 0.68 :
      screenP.y * 0.62 + screenP.x * 0.25 - 0.86;
  float seamDistance = abs(seamCoordinate);
  float pierSeam = 1.0 - smoothstep(width + pierAa,
                                    width * 3.4 + pierAa,
                                    seamDistance);
  color += float3(0.035, 0.67, 0.85) *
           (pierEdge * 0.32 + pierSeam * pierBody *
            (0.035 + 0.045 * treble));

  // A quiet, optically deep center and restrained local halo preserve the
  // black gaps. Output is opaque because Spout black is not transparency.
  float rim = smoothstep(opening - 0.018, opening + 0.002, radius) *
              (1.0 - smoothstep(opening + 0.010,
                                opening + 0.058, radius));
  color += rim * float3(0.006, 0.015, 0.025);
  color = color / (0.79 + color);
  return float4(min(color, float3(0.91, 0.91, 0.91)), 1.0);
}
