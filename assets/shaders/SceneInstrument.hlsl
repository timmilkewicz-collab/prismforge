#ifndef SCENE_INSTRUMENT_INCLUDED
#define SCENE_INSTRUMENT_INCLUDED

float2 pf_rotate2(float2 p, float angle) {
  float sn;
  float cs;
  sincos(angle, sn, cs);
  return float2(cs * p.x - sn * p.y, sn * p.x + cs * p.y);
}

void pf_four_centers(float time, float pressure, out float2 c0, out float2 c1,
                    out float2 c2, out float2 c3) {
  float ph = time * (0.09 + 0.07 * saturate(pressure));
  float spread = lerp(0.18, 0.42, saturate(pressure));
  c0 = float2(-spread + 0.07 * sin(ph), spread * 0.55 + 0.05 * cos(ph * 0.91));
  c1 = float2(spread * 0.85 + 0.06 * sin(ph * 1.17 + 1.4),
              -spread * 0.45 + 0.04 * cos(ph * 0.83 + 0.6));
  c2 = float2(-spread * 0.15 + spread * 0.75 * sin(ph * 0.67 + 2.3),
              -spread * 0.95 + 0.07 * cos(ph * 1.09 + 1.1));
  c3 = float2(spread * 0.55 + 0.05 * sin(ph * 0.53 + 0.2),
              spread * 0.75 + 0.08 * cos(ph * 1.31 + 2.0));
}

float pf_region_weight(float2 p, float2 center, float radius) {
  float d = length(p - center);
  return saturate(1.0 - smoothstep(radius * 0.25, radius, d));
}

float2 pf_center_swirl(float2 p, float2 center, float weight, float spin) {
  float2 d = p - center;
  float r2 = max(dot(d, d), 0.00008);
  float angle = spin * weight / (0.12 + r2 * 6.0);
  return pf_rotate2(d, angle) + center - p;
}

float2 pf_multi_center_field(float2 p, float time, float pressure, float spinSign) {
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, pressure, c0, c1, c2, c3);
  float rad = lerp(0.28, 0.52, saturate(pressure));
  float2 delta = 0;
  delta += pf_center_swirl(p, c0, pf_region_weight(p, c0, rad), spinSign);
  delta += pf_center_swirl(p, c1, pf_region_weight(p, c1, rad), -spinSign * 0.85);
  delta += pf_center_swirl(p, c2, pf_region_weight(p, c2, rad * 0.9), spinSign * 1.15);
  delta += pf_center_swirl(p, c3, pf_region_weight(p, c3, rad * 0.85), -spinSign * 0.7);
  return p + delta * saturate(pressure);
}

float pf_nested_complexity(float2 p, float density, float time, float chaos) {
  float r = length(p);
  float a = atan2(p.y, p.x);
  float wave = 0;
  [unroll] for (int i = 0; i < 4; ++i) {
    float fi = (float)i;
    float freq = 4.0 + fi * 2.6 + density * 6.0;
    wave += sin(freq * r - time * (0.21 + fi * 0.05) + sin(a * (2.0 + fi) + fi)) /
            (1.0 + fi * 0.55);
  }
  wave += chaos * sin(11.0 * a + 17.0 * r - time * 0.33);
  return wave;
}

float pf_operator_center_pressure(float4 controls) {
  return saturate(controls.y * 0.42 + controls.w * 0.38 + controls.x * 0.12);
}

float3 pf_center_region_tint(float3 color, float2 p, float time, float pressure) {
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, pressure, c0, c1, c2, c3);
  float rad = lerp(0.26, 0.48, saturate(pressure));
  float w0 = pf_region_weight(p, c0, rad);
  float w1 = pf_region_weight(p, c1, rad);
  float w2 = pf_region_weight(p, c2, rad);
  float w3 = pf_region_weight(p, c3, rad);
  float3 t0 = float3(0.08, 0.22, 0.55);
  float3 t1 = float3(0.55, 0.12, 0.38);
  float3 t2 = float3(0.12, 0.48, 0.32);
  float3 t3 = float3(0.42, 0.28, 0.08);
  color = lerp(color, color * (1.0 + t0), w0 * pressure * 0.55);
  color = lerp(color, color * (1.0 + t1), w1 * pressure * 0.52);
  color = lerp(color, color * (1.0 + t2), w2 * pressure * 0.48);
  color = lerp(color, color * (1.0 + t3), w3 * pressure * 0.46);
  return color;
}

float3 pf_center_rim(float3 color, float2 p, float time, float pressure, float3 tintA, float3 tintB) {
  float2 c0;
  float2 c1;
  float2 c2;
  float2 c3;
  pf_four_centers(time, pressure, c0, c1, c2, c3);
  float w0 = pf_region_weight(p, c0, 0.34);
  float w1 = pf_region_weight(p, c1, 0.32);
  float w2 = pf_region_weight(p, c2, 0.30);
  float w3 = pf_region_weight(p, c3, 0.31);
  float ring0 = pow(saturate(1.0 - abs(length(p - c0) - 0.11)), 12.0) * w0;
  float ring1 = pow(saturate(1.0 - abs(length(p - c1) - 0.10)), 12.0) * w1;
  float ring2 = pow(saturate(1.0 - abs(length(p - c2) - 0.09)), 12.0) * w2;
  float ring3 = pow(saturate(1.0 - abs(length(p - c3) - 0.10)), 12.0) * w3;
  color += tintA * ring0 * pressure;
  color += tintB * ring1 * pressure;
  color += lerp(tintA, tintB, 0.5) * ring2 * pressure * 0.85;
  color += tintA * ring3 * pressure * 0.75;
  return color;
}

float3 pf_depth_structure(float3 color, float2 p, float time, float density, float chaos) {
  float wave = pf_nested_complexity(p, density, time, chaos);
  float band = smoothstep(-0.2, 0.65, wave);
  float fil = pow(saturate(1.0 - abs(frac(wave * 0.31 + 0.5) - 0.5) * 4.0), 8.0);
  color += fil * float3(0.06, 0.08, 0.12) * (0.25 + density);
  color *= 0.92 + band * 0.08 * density;
  return color;
}

#endif
