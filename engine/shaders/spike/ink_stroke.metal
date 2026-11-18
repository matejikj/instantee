#include <metal_stdlib>

using namespace metal;

// Matches geometry::InkStrokeVertex packing for the ink stroke ribbon.
struct InkStrokeVertexIn {
  float2 position;
  float s;
  float s_norm;
  float side;
  float width_px;
};

struct InkStrokeUniforms {
  float2 canvas; // width, height (origin top-left, +Y down)
  float progress;
  float brush_grain;
  uint brush_seed;
  float3 _pad0;
  float4 ink; // straight rgba 0..1
};

struct VertexOut {
  float4 position [[position]];
  float s_norm [[user(s_norm)]];
  float side [[user(side)]];
  float s [[user(s)]];
};

vertex VertexOut vs_ink_stroke_main(uint vid [[vertex_id]],
                                    const device InkStrokeVertexIn *verts
                                    [[buffer(0)]],
                                    constant InkStrokeUniforms &u
                                    [[buffer(1)]]) {
  const InkStrokeVertexIn v = verts[vid];
  const float2 canvas = max(u.canvas, float2(1.0));
  // Pixel → NDC (Metal: origin bottom-left in NDC; pixel y grows down).
  const float x_ndc = (v.position.x / canvas.x) * 2.0 - 1.0;
  const float y_ndc = 1.0 - (v.position.y / canvas.y) * 2.0;
  VertexOut out;
  out.position = float4(x_ndc, y_ndc, 0.0, 1.0);
  out.s_norm = v.s_norm;
  out.side = v.side;
  out.s = v.s;
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

fragment float4 fs_ink_stroke_main(VertexOut in [[stage_in]],
                                   constant InkStrokeUniforms &u
                                   [[buffer(1)]]) {
  // Arc-length reveal: discard beyond the animated progress. The mesh stays
  // uploaded; only this uniform changes per frame (no CPU bitmap restamp).
  const float progress = clamp(u.progress, 0.0, 1.0);
  if (in.s_norm > progress + 1e-5) {
    discard_fragment();
  }

  const float across = 1.0 - abs(in.side);
  float coverage = smoothstep(0.0, 0.35, across);

  // Procedural brush grain along the stroke (not a CPU-uploaded bitmap).
  const uint sx = uint(in.s * 8.0);
  const uint sy = uint((in.side + 1.0) * 16.0);
  const float noise =
      unit_from_hash(mix_u32(u.brush_seed, mix_u32(sx, sy ^ 0x5C5C5C5Cu)));
  const float grain = mix(1.0, 0.55 + 0.45 * noise, clamp(u.brush_grain, 0.0, 1.0));
  coverage *= grain;

  if (coverage <= 0.001) {
    discard_fragment();
  }
  return float4(u.ink.rgb, u.ink.a * coverage);
}
