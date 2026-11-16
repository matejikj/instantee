#include <metal_stdlib>

using namespace metal;

struct VertexOut {
  float4 position [[position]];
};

vertex VertexOut vs_shared_main(uint vid [[vertex_id]]) {
  constexpr float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),
                                   float2(-1.0, 3.0)};
  VertexOut out;
  out.position = float4(positions[vid], 0.0, 1.0);
  return out;
}

// Same deterministic pattern as test_pattern.metal, flattened over opaque black
// with the CPU integer formula so encoder-ready BGRA matches the staging path.
fragment float4 fs_shared_main(VertexOut in [[stage_in]],
                               constant uint &frame [[buffer(0)]]) {
  const uint x = uint(in.position.x);
  const uint y = uint(in.position.y);
  const uint base_r = frame == 0u ? 240u : 16u;
  const uint base_g = frame == 0u ? 32u : 200u;
  const uint base_b = frame == 0u ? 64u : 96u;
  const uint base_a = frame == 0u ? 128u : 200u;
  const uint r = (base_r + (x & 15u)) & 255u;
  const uint g = (base_g + (y & 15u)) & 255u;
  const uint flat_r = (r * base_a + 127u) / 255u;
  const uint flat_g = (g * base_a + 127u) / 255u;
  const uint flat_b = (base_b * base_a + 127u) / 255u;
  return float4(float(flat_r), float(flat_g), float(flat_b), 255.0) / 255.0;
}
