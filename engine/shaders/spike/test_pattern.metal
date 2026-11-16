#include <metal_stdlib>

using namespace metal;

struct VertexOut {
  float4 position [[position]];
};

vertex VertexOut vs_main(uint vid [[vertex_id]]) {
  constexpr float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),
                                   float2(-1.0, 3.0)};
  VertexOut out;
  out.position = float4(positions[vid], 0.0, 1.0);
  return out;
}

// Deterministic time-varying RGBA8 pattern with non-opaque alpha.
// Pixel centers truncate to integer coordinates that match the CPU oracle.
fragment float4 fs_main(VertexOut in [[stage_in]],
                        constant uint &frame [[buffer(0)]]) {
  const uint x = uint(in.position.x);
  const uint y = uint(in.position.y);
  const uint base_r = frame == 0u ? 240u : 16u;
  const uint base_g = frame == 0u ? 32u : 200u;
  const uint base_b = frame == 0u ? 64u : 96u;
  const uint base_a = frame == 0u ? 128u : 200u;
  const uint r = (base_r + (x & 15u)) & 255u;
  const uint g = (base_g + (y & 15u)) & 255u;
  return float4(float(r), float(g), float(base_b), float(base_a)) / 255.0;
}
