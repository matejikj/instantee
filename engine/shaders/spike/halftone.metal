#include <metal_stdlib>

using namespace metal;

struct VertexOut {
  float4 position [[position]];
};

struct HalftoneUniforms {
  uint width;
  uint height;
  uint seed;
  uint max_samples;
  uint mode_dots; // 1 = dots, 0 = lines
  uint apply_ellipse_mask;
  float dot_scale;
  float resolution_scale;
  float4 ink;
  float4 paper;
};

vertex VertexOut vs_halftone_main(uint vid [[vertex_id]]) {
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

static float photo_luma(uint x, uint y, uint width, uint height) {
  const float u = float(x) / float(width);
  const float v = float(y) / float(height);
  return clamp(0.25 + 0.55 * u + 0.20 * (1.0 - v), 0.0, 1.0);
}

static bool ellipse_mask(float x, float y, float width, float height) {
  const float cx = width * 0.5;
  const float cy = height * 0.5;
  const float rx = width * 0.42;
  const float ry = height * 0.42;
  const float nx = (x + 0.5 - cx) / rx;
  const float ny = (y + 0.5 - cy) / ry;
  return (nx * nx + ny * ny) <= 1.0;
}

fragment float4 fs_halftone_main(VertexOut in [[stage_in]],
                                 constant HalftoneUniforms &u [[buffer(0)]]) {
  const uint x = uint(in.position.x);
  const uint y = uint(in.position.y);
  if (x >= u.width || y >= u.height) {
    return u.paper;
  }

  if (u.apply_ellipse_mask != 0u &&
      !ellipse_mask(float(x), float(y), float(u.width), float(u.height))) {
    return u.paper;
  }

  const float cell = u.dot_scale / max(u.resolution_scale, 1.0e-6);
  const float fx = (float(x) + 0.5) / cell;
  const float fy = (float(y) + 0.5) / cell;
  const int cx = int(floor(fx));
  const int cy = int(floor(fy));
  const float lx = fx - float(cx);
  const float ly = fy - float(cy);

  float luma_acc = 0.0;
  const uint samples = clamp(u.max_samples, 1u, 8u);
  for (uint i = 0u; i < samples; ++i) {
    const uint h =
        mix_u32(u.seed, mix_u32(uint(cx), mix_u32(uint(cy), i)));
    const float jx = unit_from_hash(h);
    const float jy = unit_from_hash(mix_u32(h, 0x9e3779b9u));
    const float sx = (float(cx) + jx) * cell;
    const float sy = (float(cy) + jy) * cell;
    const uint px = uint(clamp(sx, 0.0, float(u.width - 1u)));
    const uint py = uint(clamp(sy, 0.0, float(u.height - 1u)));
    luma_acc += photo_luma(px, py, u.width, u.height);
  }
  const float tone = clamp(luma_acc / float(samples), 0.0, 1.0);
  const float coverage = 1.0 - tone;

  bool ink = false;
  if (u.mode_dots == 0u) {
    const float phase = unit_from_hash(mix_u32(u.seed, uint(cy)));
    const float band = abs(fract(lx + phase) - 0.5) * 2.0;
    ink = band <= coverage;
  } else {
    const float dx = lx - 0.5;
    const float dy = ly - 0.5;
    const float dist = sqrt(dx * dx + dy * dy);
    const float radius = 0.5 * sqrt(coverage);
    ink = dist <= radius;
  }
  return ink ? u.ink : u.paper;
}
