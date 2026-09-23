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

#if QUALITY_TIER == 0
  #define ORB_COUNT 18
#elif QUALITY_TIER == 1
  #define ORB_COUNT 14
#elif QUALITY_TIER == 2
  #define ORB_COUNT 11
#else
  #define ORB_COUNT 8
#endif

// A synthetic enamel-and-light mobile. No image, cubemap or reference-media
// samples are used. Most pixels skip the expensive surface work entirely.
float4 main(PSInput input) : SV_TARGET {
  float aspect = resolution.x / max(resolution.y, 1.0);
  float2 p = (input.uv - 0.5) * float2(aspect, 1.0);
  float low = saturate(bass);
  float mid = saturate(mids);
  float treble = saturate(highs);
  float accent = saturate(hit);
  float3 color = float3(0.0010, 0.0010, 0.0014);

  // A long orbit gives the performer something that feels suspended rather
  // than screen-saver random. Bass changes spacing, never frame exposure.
  float2 hub = float2(0.055 + 0.055 * sin(time * 0.18),
                      0.012 + 0.037 * cos(time * 0.23));
  float turn = time * 0.105;
  float turnSin;
  float turnCos;
  sincos(turn, turnSin, turnCos);
  float spread = 1.0 + 0.13 * low;

  [loop] for (int i = 0; i < ORB_COUNT; ++i) {
    float seed = (float)i + 1.0;
    float a = seed * 2.39996323 + 0.18 * sin(time * 0.17 + seed * 0.41);
    float s;
    float c;
    sincos(a, s, c);
    float radial = 0.25 + 0.75 * sqrt(frac(seed * 0.61803399));
    float2 place = float2(0.60 * radial * c, 0.39 * radial * s);
    place += float2(0.026 * sin(time * (0.22 + seed * 0.006) + seed * 1.7),
                    0.025 * cos(time * (0.19 + seed * 0.008) + seed * 1.2));
    float2 center = hub + spread *
        float2(turnCos * place.x - turnSin * place.y,
               turnSin * place.x + turnCos * place.y);

    // Stratified depth gives even the lower tiers both distant and near
    // pieces. The larger foreground pieces have stronger reflected light.
    float depth = frac(seed * 0.75487767);
    float radius = (0.050 + 0.072 * depth) *
                   (0.78 + 0.33 * frac(seed * 0.75487767));
    radius *= 1.0 + 0.018 * mid;
    float2 delta = p - center;
    float d2 = dot(delta, delta);
    float reach = radius + 0.071;
    if (d2 >= reach * reach) {
      continue;
    }

    float d = sqrt(max(d2, 0.0));
    float focus = saturate(1.0 - abs(depth - 0.70) * 1.35);
    float softness = 0.0016 + 0.0048 * (1.0 - focus);
    float body = 1.0 - smoothstep(radius - softness,
                                   radius + softness, d);
    float z = sqrt(saturate(1.0 - d2 / max(radius * radius, 0.0001)));
    float3 normal = normalize(float3(delta / radius, z + 0.0001));

    // Three reflected lights and a soft environment ribbon sell curved,
    // glossy volume while keeping most of each surface near-black.
    float key = pow(saturate(dot(normal,
                    float3(-0.33, 0.57, 0.75))),
                    12.0 + 15.0 * focus);
    float fill = pow(saturate(dot(normal,
                     float3(0.64, -0.28, 0.72))), 21.0);
    float environment = pow(saturate(1.0 - abs(
                            normal.x * 0.66 + normal.y * 0.42 -
                            normal.z * 0.31)), 6.0);
    float grazing = pow(saturate(1.0 - normal.z), 1.9);
    float3 enamel = float3(0.017, 0.020, 0.025);
    enamel += (0.032 + 0.029 * depth) *
              saturate(normal.y * 0.42 + normal.z * 0.36);
    enamel += environment * float3(0.081, 0.093, 0.106);
    enamel += grazing * float3(0.092, 0.108, 0.127);
    enamel += key * float3(0.44, 0.50, 0.55) *
              (0.68 + 0.32 * focus);
    enamel += fill * float3(0.19, 0.24, 0.27) * focus;
    color = lerp(color, enamel, body);

    // Each piece bears a different, rotating partial incised arc. It is an
    // abstract etched surface, not an equatorial wrap copied from the clip.
    float stripeAngle = 0.62 * seed + 0.21 * time *
                        (0.74 + 0.22 * frac(seed * 0.37));
    float ss;
    float cs;
    sincos(stripeAngle, ss, cs);
    float2 local = float2(cs * normal.x + ss * normal.y,
                         -ss * normal.x + cs * normal.y);
    float engraving = local.y + 0.33 * normal.z +
                      0.11 * sin(3.5 * local.x + seed * 1.9 +
                                 time * 0.34);
    float arc = 1.0 - smoothstep(0.070, 0.135 + 0.025 *
                                 (1.0 - focus), abs(engraving));
    float broadArc = 1.0 - smoothstep(0.12, 0.27, abs(engraving));
    float segment = smoothstep(-0.86, -0.62, local.x) *
                    (1.0 - smoothstep(0.54, 0.82, local.x));
    float luster = arc * segment * body;

    float palette = frac(seed * 0.41421356);
    float3 emission = float3(1.0, 0.045, 0.34);
    if (palette > 0.33 && palette < 0.67) {
      emission = float3(0.30, 1.0, 0.035);
    } else if (palette >= 0.67) {
      emission = float3(1.0, 0.43, 0.025);
    }
    float beatSelect = palette > 0.66 ? 1.0 : 0.0;
    float energy = 1.16 + 0.10 * treble +
                   0.18 * accent * beatSelect;
    color += emission * luster * energy *
             (0.75 + 0.25 * focus);
    color += emission * broadArc * segment * body * 0.11;

    // The haze belongs only to each engraved object; there is no full-frame
    // bloom, luminance jump or hidden audio-driven flash.
    float halo = (1.0 - smoothstep(radius + 0.001,
                                   radius + 0.071, d)) * (1.0 - body);
    color += emission * halo * 0.052 *
             (0.45 + 0.55 * focus) *
             (0.35 + 0.65 * segment);
  }

  color = color / (0.83 + color);
  return float4(min(color, float3(0.88, 0.88, 0.88)), 1.0);
}
