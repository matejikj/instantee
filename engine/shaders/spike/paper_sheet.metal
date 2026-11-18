#include <metal_stdlib>

using namespace metal;

struct VertexOut {
  float4 position [[position]];
};

struct PaperSheetUniforms {
  uint width;
  uint height;
  uint seed;
  float fiber;
  float roughness;
  float color_wash;
  float edge_thickness;
  float shadow_bias;
  float normal_variation;
  float4 pulp_color;
  float4 wash_color;
};

vertex VertexOut vs_paper_sheet_main(uint vid [[vertex_id]]) {
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

static float4 shade_pixel(constant PaperSheetUniforms &u, uint x, uint y) {
  const float fx = float(x) + 0.5;
  const float fy = float(y) + 0.5;
  const float w = max(float(u.width), 1.0);
  const float h = max(float(u.height), 1.0);
  const float nx = (fx / w) * 2.0 - 1.0;
  const float ny = (fy / h) * 2.0 - 1.0;

  // Matte pulp base with a soft radial falloff (watercolor paper plate).
  const float falloff = clamp(1.0 - 0.12 * (nx * nx + ny * ny), 0.0, 1.0);
  float3 rgb = u.pulp_color.rgb * falloff;

  // Fiber streaks: anisotropic noise along a gentle diagonal grain.
  const float fiber_a = unit_noise(u.seed, x, y / 2u, 1u);
  const float fiber_b = unit_noise(u.seed, x / 3u, y, 2u);
  const float fiber_mix = fiber_a * 0.65 + fiber_b * 0.35;
  rgb *= 1.0 + (fiber_mix - 0.5) * 0.55 * u.fiber;

  // Roughness: fine isotropic speckles (cheap normal-less micro detail).
  const float rough = unit_noise(u.seed, x, y, 3u);
  rgb *= 1.0 + (rough - 0.5) * 0.35 * u.roughness;

  // Color wash toward a lighter tint.
  const float wash_mask =
      clamp(0.35 + 0.65 * unit_noise(u.seed, x / 2u, y / 2u, 4u), 0.0, 1.0);
  rgb = mix(rgb, u.wash_color.rgb, wash_mask * u.color_wash);

  // Fake normal variation: fixed key light with seeded tilt.
  const float nx_n =
      (unit_noise(u.seed, x, y, 5u) - 0.5) * 2.0 * u.normal_variation;
  const float ny_n =
      (unit_noise(u.seed, x, y, 6u) - 0.5) * 2.0 * u.normal_variation;
  const float3 n = normalize(float3(nx_n, ny_n, 1.0));
  const float3 light = normalize(float3(-0.25, -0.4, 1.0));
  const float lambert = clamp(dot(n, light), 0.0, 1.0);
  rgb *= 0.88 + 0.20 * lambert;

  // Soft edge darkening from sheet thickness (rim), scaled by shadow_bias.
  const float edge_x = min(fx, w - fx) / w;
  const float edge_y = min(fy, h - fy) / h;
  const float edge = clamp(min(edge_x, edge_y) / max(u.edge_thickness, 0.001),
                           0.0, 1.0);
  const float rim = (1.0 - edge) * (0.25 + 0.75 * u.shadow_bias);
  rgb *= 1.0 - 0.22 * rim;

  return float4(clamp(rgb, 0.0, 1.0), u.pulp_color.a);
}

fragment float4 fs_paper_sheet_main(VertexOut in [[stage_in]],
                                    constant PaperSheetUniforms &u
                                    [[buffer(0)]]) {
  const uint x = uint(in.position.x);
  const uint y = uint(in.position.y);
  if (x >= u.width || y >= u.height) {
    return u.pulp_color;
  }
  return shade_pixel(u, x, y);
}
