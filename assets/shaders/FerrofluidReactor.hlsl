cbuffer SceneInputs : register(b0) {
  float time;
  float bass;
  float mids;
  float highs;
  float hit;
  float2 resolution;
  float4 reactive;
  float4 sceneParams;
  float4 musicalA;
  float4 musicalB;
};

struct PSInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  p -= float2(0.020 * sin(time * 0.17),
              0.014 * cos(time * 0.13));
  float radius = max(length(p), 0.0005);
  float angle = atan2(p.y, p.x);
  float low = saturate(bass);
  float mid = saturate(mids);
  float treble = saturate(highs);
  float accent = saturate(hit);

  // This is a full-frame liquid-metal flow, not a single radial silhouette.
  // A logarithmic spiral gives the center much denser, apparently receding
  // detail while the outer flow continues through the sides of a 16:9 frame.
  float spiral = angle + 2.15 * log(radius + 0.16) - time * 0.105;
  spiral += (0.11 + 0.045 * low) *
            sin(5.0 * angle - 9.0 * radius + time * 0.16);
  float warpA = sin(4.0 * spiral - 13.0 * radius + time * 0.19);
  float warpB = sin(7.0 * spiral + 19.0 * radius - time * 0.28);
  float warpC = sin(3.0 * spiral + 27.0 * radius + time * 0.11);
  float flow = 12.0 * spiral + 32.0 * radius - time * 0.49 +
               0.82 * warpA + (0.36 + 0.10 * mid) * warpB;
  float counterflow = 17.0 * spiral - 43.0 * radius + time * 0.31 -
                      0.66 * warpA + 0.27 * warpC;

  // Wide pigment pools, narrow dark fractures and finer specular striae are
  // separate scales. None depends on global audio-driven exposure.
  float hueField = 0.5 + 0.5 * sin(3.0 * spiral + 6.0 * radius -
                                    time * 0.12 + 0.62 * warpB);
  float molten = saturate(0.5 + 0.5 * sin(flow));
  float reverse = saturate(0.5 + 0.5 * sin(counterflow));
  float fold = saturate(0.5 + 0.5 * sin(7.0 * spiral -
                                     12.0 * radius + time * 0.21 +
                                     0.50 * warpC));
  float fissure = 1.0 - smoothstep(0.025, 0.16,
                                 abs(sin(flow - counterflow)));
  float silver = pow(saturate(0.5 + 0.5 * sin(flow + 1.45)), 18.0);
  float bronze = pow(saturate(0.5 + 0.5 * sin(counterflow - 0.60)), 11.0);

  float3 indigo = float3(0.037, 0.012, 0.12);
  float3 crimson = float3(0.66, 0.018, 0.16);
  float3 cyan = float3(0.015, 0.43, 0.60);
  float3 pigment = lerp(crimson, cyan, hueField);
  pigment = lerp(pigment, float3(0.34, 0.045, 0.48), fold * 0.37);
  float3 color = indigo + pigment * (0.16 + 0.79 * molten);
  color += reverse * (1.0 - hueField) * float3(0.16, 0.015, 0.095);
  color += reverse * hueField * float3(0.010, 0.13, 0.18);
  color *= 1.0 - 0.66 * fissure;
  color += bronze * float3(0.42, 0.20, 0.035) *
           (0.30 + 0.70 * (1.0 - hueField));
  color += silver * float3(0.37, 0.62, 0.76) *
           (0.48 + 0.26 * treble);

  // Staggered depth ribs disappear into the throat. The angular warp breaks
  // their circles into interleaved, sculptural sheets of chrome and light.
  float depthPhase = 30.0 * log(radius + 0.20) - time * 0.43 +
                     0.74 * warpA + 0.25 * warpB;
  float depthRib = pow(saturate(0.5 + 0.5 * sin(depthPhase)), 10.0);
  float depthShadow = 1.0 - smoothstep(-0.38, 0.18, sin(depthPhase + 0.85));
  color *= 1.0 - 0.32 * depthShadow;
  color += depthRib * lerp(float3(0.36, 0.035, 0.22),
                           float3(0.035, 0.36, 0.48), hueField) * 0.68;

#if QUALITY_TIER < 3
  // Microfilaments sit only on moving metal edges; they never flash the
  // entire output. The safety tier omits this highest-frequency layer.
  float filamentPhase = 29.0 * spiral + 52.0 * radius -
                        time * 0.35 + 0.82 * warpB;
  float filament = pow(saturate(0.5 + 0.5 * sin(filamentPhase)), 24.0);
  float glint = pow(saturate(0.5 + 0.5 * sin(filamentPhase -
                       counterflow)), 29.0);
  color += filament * float3(0.08, 0.30, 0.37) *
           (0.38 + 0.62 * molten);
  color += glint * float3(0.31, 0.27, 0.36) *
           (0.20 + 0.80 * reverse) * (0.65 + 0.20 * accent);
#endif

  // Distance shading makes the central opening feel deep rather than like
  // a flat disk. The hit signal only excites its narrow rim and nearby ribs.
  float throat = smoothstep(0.055, 0.22, radius);
  float throatRim = smoothstep(0.064, 0.108, radius) *
                    (1.0 - smoothstep(0.125, 0.22, radius));
  float distanceShade = 0.72 + 0.28 * smoothstep(0.07, 0.62, radius);
  color *= throat * distanceShade;
  color += throatRim * float3(0.035, 0.24, 0.32) *
           (0.70 + 0.22 * accent);
  color += depthRib * throatRim * float3(0.28, 0.08, 0.16) * 0.35;

  float4 controls = saturate(sceneParams);
  float assist = musicalB.w;
  float pull = lerp(0.15, 1.5, saturate(controls.x + assist * 0.14 * (musicalA.z - 0.35)));
  float viscosity = lerp(0.45, 1.6, controls.y);
  float centers = lerp(1.0, 3.0, saturate(controls.z + assist * 0.1 * musicalA.y));
  float crystal = saturate(controls.w - 0.15 * assist * musicalB.y);
  float centerPres = pf_operator_center_pressure(controls);
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, saturate(centers / 3.0 + centerPres * 0.5), c0, c1, c2, c3);
  float2 magnetA = lerp(float2(-0.34 + 0.06 * sin(time * 0.19), 0.1 * cos(time * 0.15)), c0, centerPres);
  float2 magnetB = lerp(float2(0.30, -0.16 + 0.05 * sin(time * 0.13)), c1, centerPres);
  float2 magnetC = lerp(float2(0.02 * cos(time * 0.11), 0.28), c2, centerPres);
  float2 magnetD = c3;
  float field = exp(-length(p - magnetA) * (4.0 + 8.0 * viscosity));
  if (centers > 1.2) field += exp(-length(p - magnetB) * (5.0 + 7.0 * viscosity));
  if (centers > 2.2) field += exp(-length(p - magnetC) * (6.0 + 6.0 * viscosity));
  field += exp(-length(p - magnetD) * (5.5 + 5.5 * viscosity)) * centerPres;
  color += field * float3(0.04, 0.20, 0.26) * pull;
  float lattice = pow(saturate(0.5 + 0.5 * sin(length(p - magnetA) * 48.0)), 16.0);
  color += lattice * field * float3(0.55, 0.62, 0.7) * crystal;

  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, float3(0.05, 0.35, 0.42), float3(0.42, 0.12, 0.28));
  color = pf_depth_structure(color, p, time, viscosity, pull * 0.22);
  color = color / (0.79 + color);
  return float4(min(color, float3(0.91, 0.91, 0.91)), 1.0);
}
