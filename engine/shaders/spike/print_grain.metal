#include <metal_stdlib>

using namespace metal;

struct VertexOut {
  float4 position [[position]];
};

struct PrintGrainUniforms {
  uint width;
  uint height;
  uint seed;
  float intensity;
  float registration_x_px;
  float registration_y_px;
  float defect_density;
  float4 registration_tint;
  float4 defect_color;
  float4 base_paper;
};

vertex VertexOut vs_print_grain_main(uint vid [[vertex_id]]) {
  constexpr float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),
                                   float2(-1.0, 3.0)};
  VertexOut out;
  out.position = float4(positions[vid], 0.0, 1.0);
  return out;
}

static uint mix_u32(uint a, uint b) {
  uint x = a * 1664525u + b + 1013904223u;
  x ^= x >> 16;
  x *= 2246822519u;
  x ^= x >> 13;
  return x;
}

static float unit_from_hash(uint h) {
  return float(h & 0x00FFFFFFu) / float(0x01000000u);
}

static float unit_noise(uint seed, uint x, uint y, uint channel) {
  return unit_from_hash(
      mix_u32(seed, mix_u32(x, mix_u32(y, channel ^ 0xA5A5A5A5u))));
}

static float4 sample_base(constant PrintGrainUniforms &u, float fx, float fy) {
  const float cx = float(u.width) * 0.5;
  const float cy = float(u.height) * 0.5;
  const float nx = (fx + 0.5 - cx) / max(cx, 1.0);
  const float ny = (fy + 0.5 - cy) / max(cy, 1.0);
  const float falloff = clamp(1.0 - 0.18 * (nx * nx + ny * ny), 0.0, 1.0);
  return float4(u.base_paper.rgb * falloff, u.base_paper.a);
}

static float4 composite_over(float4 dst, float4 src, float coverage) {
  coverage = clamp(coverage, 0.0, 1.0);
  if (coverage <= 0.0) {
    return dst;
  }
  const float sa = src.a * coverage;
  if (sa >= 1.0) {
    return float4(src.rgb, 1.0);
  }
  const float da = dst.a;
  const float out_a = sa + da * (1.0 - sa);
  if (out_a <= 0.0) {
    return float4(0.0);
  }
  const float3 out_rgb =
      (src.rgb * sa + dst.rgb * da * (1.0 - sa)) / out_a;
  return float4(out_rgb, out_a);
}

fragment float4 fs_print_grain_main(VertexOut in [[stage_in]],
                                    constant PrintGrainUniforms &u
                                    [[buffer(0)]]) {
  const uint x = uint(in.position.x);
  const uint y = uint(in.position.y);
  if (x >= u.width || y >= u.height) {
    return u.base_paper;
  }

  float4 pixel = sample_base(u, float(x), float(y));

  const float n = unit_noise(u.seed, x, y, 0u);
  const float grain = 1.0 + (n - 0.5) * 2.0 * u.intensity;
  pixel.rgb *= grain;

  const float sx = float(x) + u.registration_x_px;
  const float sy = float(y) + u.registration_y_px;
  const float clamped_x = clamp(sx, 0.0, float(u.width - 1u));
  const float clamped_y = clamp(sy, 0.0, float(u.height - 1u));
  const float4 plate = sample_base(u, clamped_x, clamped_y);
  const float plate_luma =
      0.2126 * plate.r + 0.7152 * plate.g + 0.0722 * plate.b;
  const float plate_cover =
      clamp((1.0 - plate_luma) * 0.22 * (0.35 + u.intensity), 0.0, 1.0);
  pixel = composite_over(pixel, u.registration_tint, plate_cover);

  const float d = unit_noise(u.seed, x, y, 7u);
  if (d < u.defect_density) {
    const float cover = 0.35 + 0.45 * unit_noise(u.seed, x, y, 8u);
    pixel = composite_over(pixel, u.defect_color, cover);
  }

  return pixel;
}
