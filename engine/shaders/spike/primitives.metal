#include <metal_stdlib>

using namespace metal;

struct VertexOut {
  float4 position [[position]];
};

struct PrimitiveUniforms {
  float2 canvas; // width, height
  float2 _pad0;  // float4 alignment
  float4 rect;   // x, y, w, h (line: x,y = start)
  float4 color;  // straight rgba 0..1
  float2 line_end;
  float corner_radius;
  float stroke_width;
  uint kind; // 2 rect, 3 ellipse, 4 rounded, 5 line
  uint _pad1;
};

vertex VertexOut vs_prim_main(uint vid [[vertex_id]]) {
  constexpr float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),
                                   float2(-1.0, 3.0)};
  VertexOut out;
  out.position = float4(positions[vid], 0.0, 1.0);
  return out;
}

static float cover_rect(float2 p, float4 rect) {
  return step(rect.x, p.x) * step(p.x, rect.x + rect.z) * step(rect.y, p.y) *
         step(p.y, rect.y + rect.w);
}

static float cover_ellipse(float2 p, float4 rect) {
  const float2 c = rect.xy + rect.zw * 0.5;
  const float2 r = rect.zw * 0.5;
  const float2 n = (p - c) / max(r, float2(1e-5));
  return step(dot(n, n), 1.0);
}

static float sd_rounded_rect(float2 p, float4 rect, float radius) {
  const float2 c = rect.xy + rect.zw * 0.5;
  const float2 half_size = rect.zw * 0.5;
  const float rad = clamp(radius, 0.0, min(half_size.x, half_size.y));
  const float2 q = abs(p - c) - (half_size - rad);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rad;
}

static float cover_line(float2 p, float2 a, float2 b, float width) {
  const float2 pa = p - a;
  const float2 ba = b - a;
  const float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-5), 0.0, 1.0);
  const float d = length(pa - ba * h);
  return step(d, max(0.5, width * 0.5));
}

fragment float4 fs_prim_main(VertexOut in [[stage_in]],
                             constant PrimitiveUniforms &u [[buffer(0)]]) {
  // Framebuffer pixel coords: origin top-left, +Y down.
  const float2 pixel = in.position.xy;
  float cover = 0.0;
  if (u.kind == 2u) {
    cover = cover_rect(pixel, u.rect);
  } else if (u.kind == 3u) {
    cover = cover_ellipse(pixel, u.rect);
  } else if (u.kind == 4u) {
    cover = step(sd_rounded_rect(pixel, u.rect, u.corner_radius), 0.0);
  } else if (u.kind == 5u) {
    cover = cover_line(pixel, u.rect.xy, u.line_end, u.stroke_width);
  }
  if (cover <= 0.0) {
    discard_fragment();
  }
  return float4(u.color.rgb, u.color.a * cover);
}
