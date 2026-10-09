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

// All geometry is analytic and original. A logarithmic hex metric creates an
// endless tunnel without sampling textures or copying presets. The many small
// details share one continuous timebase.
float4 main(PSInput input) : SV_TARGET {
  const float tau = 6.28318530718;
  float4 controls = saturate(sceneParams);
  float assist = musicalB.w;
  float centerPres = pf_operator_center_pressure(controls);
  float symmetryOrder = lerp(4.0, 8.0, saturate(controls.x + assist * 0.16 * (musicalA.z - 0.4)));
  float spin = lerp(0.55, 1.65, controls.y);
  float2 p = (input.uv - 0.5) *
             float2(resolution.x / max(resolution.y, 1.0), 1.0);
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, centerPres, c0, c1, c2, c3);
  p -= float2(0.34 * sin(time * 0.17), 0.18 * cos(time * 0.13)) * controls.z;
  p = lerp(p, pf_multi_center_field(p, time, centerPres, -1.2), centerPres * 0.72);
  float low = saturate(bass);
  float mid = saturate(mids);
  float high = saturate(highs);
  float accent = saturate(hit);

  // A little camera drift and a depth-dependent rotation make adjacent walls
  // move at different apparent speeds. Bass deforms the portal, not exposure.
  p -= float2(0.018 * sin(time * 0.31), 0.014 * cos(time * 0.23));
  float radius = max(length(p), 0.0001);
  float rawAngle = atan2(p.y, p.x);
  float logarithm = log2(radius + 0.038);
  float whirl = time * 0.31 * spin + logarithm * 0.40 +
                (0.055 + 0.035 * low) *
                    sin(6.0 * rawAngle - time * 0.83 + 8.0 * radius);
  float sn;
  float cs;
  sincos(whirl, sn, cs);
  float2 q = float2(cs * p.x - sn * p.y, sn * p.x + cs * p.y);
  float angle = atan2(q.y, q.x);
  float2 aq = abs(q);
  float hexRadius = max(aq.x * 0.8660254 + aq.y * 0.5, aq.y);

  // Roughly fifteen continuously travelling hex levels cover the frame.
  // The floor is deliberately finite, even at the exact vanishing point.
  float phase = 3.40 * log2(hexRadius + 0.036) - time * 1.18;
  phase += 0.105 * low * sin(6.0 * angle + phase * 0.7);
  float lane = floor(phase);
  float cycle = frac(phase);
  float edgeDistance = min(cycle, 1.0 - cycle);
  float antialias = clamp(fwidth(phase) * 0.65, 0.008, 0.11);
  float gate = 1.0 - smoothstep(0.028, 0.075 + antialias, edgeDistance);
  float hotEdge = 1.0 - smoothstep(0.004, 0.025 + antialias, edgeDistance);
  float bevel = 1.0 - smoothstep(0.075, 0.20 + antialias, edgeDistance);
  float wall = smoothstep(0.08, 0.22, cycle) *
               (1.0 - smoothstep(0.77, 0.96, cycle));

  // Every hex side contains offset machine facets, engraved diagonal ribs,
  // and small luminous diamonds. At the outer rim these become broad plates;
  // deeper in the tunnel the details collapse into a bright woven core.
  float side = frac((angle + 3.14159265359) / tau * symmetryOrder);
  angle += (controls.w - 0.5) * 1.4 * sin(4.0 * logarithm + time * 0.2);
  float sideBoundary = min(side, 1.0 - side);
  float sideSeam = 1.0 - smoothstep(0.007, 0.035, sideBoundary);
  float panel = frac(side * 5.0 + lane * 0.371);
  float panelBoundary = min(panel, 1.0 - panel);
  float panelSeam = 1.0 - smoothstep(0.012, 0.065, panelBoundary);
  float facet = 0.38 + 0.62 * pow(abs(sin(angle * 3.0 + 0.45)), 2.0);
  float inset = smoothstep(0.08, 0.18, panelBoundary);
  float chevronLine = abs(cycle - (0.31 + 0.29 * abs(panel - 0.5)));
  float chevron = 1.0 - smoothstep(0.007, 0.030 + antialias,
                                 chevronLine);
  float diamondDistance = abs(panel - 0.5) * 1.65 +
                          abs(cycle - 0.48) * 1.35;
  float diamond = 1.0 - smoothstep(0.10, 0.18 + antialias,
                                  diamondDistance);
  float etched = pow(saturate(0.5 + 0.5 *
                      sin(31.0 * angle + 21.0 * logarithm -
                          time * 0.39)), 12.0);

  // Continuous radial light cables and staggered shard trains keep the
  // negative space active instead of leaving a flat black/purple field.
  float filament = pow(saturate(0.5 + 0.5 *
                       cos(24.0 * angle + 2.7 * logarithm -
                           time * 1.12)), 15.0);
  float secondFilament = pow(saturate(0.5 + 0.5 *
                             sin(36.0 * angle - 3.8 * logarithm +
                                 time * 0.86)), 22.0);
  float shardCell = frac(18.0 * (angle + 3.14159265359) / tau +
                         lane * 0.41);
  float shard = 1.0 - smoothstep(0.06, 0.16 + antialias,
                                abs(shardCell - 0.5) +
                                    abs(cycle - 0.54) * 0.82);
  float depthFade = smoothstep(0.018, 0.20, hexRadius);
  float edgeAccent = 0.8 + 0.2 * high + 0.12 * accent;

  // Adjacent levels alternate saturated pigment instead of merging into one
  // pastel haze. The palette has no global time step: a slow, continuous
  // tint drift gives motion without changing every wall at once.
  float paletteIndex = frac(lane / 3.0);
  float3 pigment = float3(0.97, 0.045, 0.57);
  if (paletteIndex > 0.25 && paletteIndex < 0.60) {
    pigment = float3(0.21, 0.92, 0.075);
  } else if (paletteIndex >= 0.60) {
    pigment = float3(0.045, 0.73, 0.95);
  }
  float3 complementary = float3(0.11, 0.79, 0.49);
  if (paletteIndex > 0.25 && paletteIndex < 0.60) {
    complementary = float3(0.83, 0.075, 0.70);
  }
  float tintDrift = 0.10 * (0.5 + 0.5 *
                               sin(time * 0.17 + lane * 0.31));
  pigment = lerp(pigment, complementary, tintDrift);

  float3 color = float3(0.009, 0.006, 0.023);
  color += depthFade * float3(0.010, 0.010, 0.030);
  color += depthFade * wall * pigment *
           (0.18 + 0.20 * facet + 0.08 * inset + 0.055 * etched);
  color += depthFade * bevel * complementary * (0.12 + 0.09 * facet);
  color += depthFade * gate * pigment * (0.53 + 0.12 * facet) * edgeAccent;
  color += depthFade * hotEdge * float3(0.12, 0.19, 0.22);
  color += depthFade * wall * panelSeam * complementary * 0.21;
  color += depthFade * wall * sideSeam * float3(0.10, 0.12, 0.19);
  color += depthFade * wall * chevron * pigment * 0.31;
  color += depthFade * wall * diamond * complementary * 0.43;
  color += depthFade * wall * shard * pigment * 0.44;
  color += depthFade * filament * complementary *
           (0.13 + 0.13 * (1.0 - wall));
  color += depthFade * secondFilament * pigment * 0.11;

  // The vanishing point is an articulated spiral with a darker center to
  // preserve spatial depth. Live flash-risk measurement is still required.
  float throat = 1.0 - smoothstep(0.02, 0.16, hexRadius);
  float coreSpiral = pow(saturate(0.5 + 0.5 *
                          sin(9.0 * rawAngle + 13.0 * logarithm -
                              time * 1.71)), 5.0);
  color += throat * (float3(0.055, 0.018, 0.11) +
                     coreSpiral * float3(0.12, 0.035, 0.15));
  color *= 0.40 + 0.60 * smoothstep(0.004, 0.055, hexRadius);
  float localSym = 1.0 + centerPres * 0.35 *
      sin(6.0 * atan2(p.y - c0.y, p.x - c0.x) + time * 0.4);
  color *= localSym;
  color = pf_center_region_tint(color, p, time, centerPres);
  color = pf_center_rim(color, p, time, centerPres, pigment, complementary);
  color = pf_depth_structure(color, p, time, controls.y, spin * 0.25);
  color = color / (0.70 + color);
  return float4(min(color, float3(0.92, 0.92, 0.92)), 1.0);
}
