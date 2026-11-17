#include <instantee/core_model/transform.hpp>
#include <instantee/typography/glyph_transform.hpp>
#include <instantee/typography/kinetic_layout.hpp>
#include <instantee/typography/text_shape.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

using instantee::core::nearly_equal;
using instantee::core::transform_point;
using instantee::core::Vec3;
using instantee::typography::apply_glyph_transforms;
using instantee::typography::build_kinetic_layout;
using instantee::typography::GlyphPivotMode;
using instantee::typography::GlyphPlacement;
using instantee::typography::GlyphTransformSpec;
using instantee::typography::make_anchor_preserving_zoom;
using instantee::typography::ShapedText;
using instantee::typography::TransformedGlyph;

[[nodiscard]] ShapedText
make_run(std::initializer_list<std::int32_t> advances) {
  ShapedText shaped;
  std::uint32_t cluster = 0;
  for (std::int32_t adv : advances) {
    GlyphPlacement g;
    g.glyph_id = static_cast<std::uint32_t>(shaped.glyphs.size() + 1);
    g.cluster = cluster;
    g.x_advance_26_6 = adv;
    shaped.glyphs.push_back(g);
    shaped.total_x_advance_26_6 += adv;
    // One ASCII byte per synthetic glyph.
    ++cluster;
  }
  shaped.grapheme_count = static_cast<std::uint32_t>(shaped.glyphs.size());
  return shaped;
}

} // namespace

int main() {
  // Three glyphs: advances 10px, 20px, 10px (26.6 = *64).
  const std::string utf8 = "abc";
  auto shaped = make_run({10 * 64, 20 * 64, 10 * 64});
  auto layout = build_kinetic_layout(utf8, shaped);
  expect(layout.has_value(), "build layout");
  if (!layout) {
    return EXIT_FAILURE;
  }

  // Identity: origins match kinetic pens; pivot maps to itself.
  {
    GlyphTransformSpec spec;
    auto out = apply_glyph_transforms(*layout, shaped, spec);
    expect(out.has_value() && out->size() == 3, "identity size");
    if (out) {
      expect(nearly_equal((*out)[0].origin_x_px, 0.0), "g0 origin x");
      expect(nearly_equal((*out)[1].origin_x_px, 10.0), "g1 origin x");
      expect(nearly_equal((*out)[2].origin_x_px, 30.0), "g2 origin x");
      for (std::size_t i = 0; i < out->size(); ++i) {
        const double adv =
            static_cast<double>(shaped.glyphs[i].x_advance_26_6) / 64.0;
        const Vec3 pivot_local{adv * 0.5, 0, 0};
        const Vec3 mapped = transform_point((*out)[i].matrix, pivot_local);
        expect(nearly_equal(mapped.x, (*out)[i].pivot_x_px) &&
                   nearly_equal(mapped.y, (*out)[i].pivot_y_px),
               "identity pivot fixed");
      }
    }
  }

  // Uniform scale about glyph centers: centers stay put, origins move in.
  {
    GlyphTransformSpec spec;
    spec.scale_x = 2.0;
    spec.scale_y = 2.0;
    spec.pivot = GlyphPivotMode::glyph_center;
    auto out = apply_glyph_transforms(*layout, shaped, spec);
    expect(out.has_value(), "scale center");
    if (out) {
      for (std::size_t i = 0; i < out->size(); ++i) {
        const double adv =
            static_cast<double>(shaped.glyphs[i].x_advance_26_6) / 64.0;
        const double base =
            static_cast<double>(layout->glyph_origin_x_26_6[i]) / 64.0;
        const double expected_pivot = base + adv * 0.5;
        expect(nearly_equal((*out)[i].pivot_x_px, expected_pivot),
               "scale pivot x");
        const Vec3 mapped =
            transform_point((*out)[i].matrix, Vec3{adv * 0.5, 0, 0});
        expect(nearly_equal(mapped, Vec3{expected_pivot, 0, 0}),
               "scale keeps center");
        // Origin is left of center after 2x scale about center.
        expect((*out)[i].origin_x_px < expected_pivot, "scaled origin left");
      }
    }
  }

  // Rotation about glyph origin: origin fixed, +X maps toward +Y.
  {
    GlyphTransformSpec spec;
    spec.rotation_z_rad = std::numbers::pi_v<double> / 2.0;
    spec.pivot = GlyphPivotMode::glyph_origin;
    auto out = apply_glyph_transforms(*layout, shaped, spec);
    expect(out.has_value(), "rotate origin");
    if (out) {
      const TransformedGlyph &g1 = (*out)[1];
      expect(nearly_equal(g1.origin_x_px, 10.0) &&
                 nearly_equal(g1.origin_y_px, 0.0),
             "rotate keeps origin");
      const Vec3 tip = transform_point(g1.matrix, Vec3{4, 0, 0});
      expect(nearly_equal(tip, Vec3{10.0, 4.0, 0.0}), "rotate +X to +Y");
    }
  }

  // Skew about center: center fixed.
  {
    GlyphTransformSpec spec;
    spec.skew_x_rad = 0.3;
    spec.pivot = GlyphPivotMode::glyph_center;
    auto out = apply_glyph_transforms(*layout, shaped, spec);
    expect(out.has_value(), "skew");
    if (out) {
      const double adv =
          static_cast<double>(shaped.glyphs[1].x_advance_26_6) / 64.0;
      const Vec3 mapped =
          transform_point((*out)[1].matrix, Vec3{adv * 0.5, 0, 0});
      expect(nearly_equal(mapped.x, (*out)[1].pivot_x_px) &&
                 nearly_equal(mapped.y, (*out)[1].pivot_y_px),
             "skew keeps pivot");
    }
  }

  // Squeeze (non-uniform scale) about center: pivot fixed.
  {
    GlyphTransformSpec spec;
    spec.scale_x = 0.5;
    spec.scale_y = 1.25;
    spec.pivot = GlyphPivotMode::glyph_center;
    auto out = apply_glyph_transforms(*layout, shaped, spec);
    expect(out.has_value(), "squeeze");
    if (out) {
      const double adv =
          static_cast<double>(shaped.glyphs[0].x_advance_26_6) / 64.0;
      const Vec3 mapped =
          transform_point((*out)[0].matrix, Vec3{adv * 0.5, 0, 0});
      expect(nearly_equal(mapped, Vec3{(*out)[0].pivot_x_px, 0, 0}),
             "squeeze keeps pivot");
    }
  }

  // Tracking shifts later glyphs without changing glyph 0.
  {
    GlyphTransformSpec spec;
    spec.tracking_26_6 = 64; // +1px per index
    auto out = apply_glyph_transforms(*layout, shaped, spec);
    expect(out.has_value(), "tracking");
    if (out) {
      expect(nearly_equal((*out)[0].origin_x_px, 0.0), "track g0");
      expect(nearly_equal((*out)[1].origin_x_px, 11.0), "track g1");
      expect(nearly_equal((*out)[2].origin_x_px, 32.0), "track g2");
    }
  }

  // Anchor-preserving zoom: shared anchor stays fixed for every glyph.
  {
    const double ax = 15.0;
    const double ay = 0.0;
    auto spec = make_anchor_preserving_zoom(1.5, ax, ay);
    auto out = apply_glyph_transforms(*layout, shaped, spec);
    expect(out.has_value(), "anchor zoom");
    if (out) {
      for (const TransformedGlyph &g : *out) {
        expect(nearly_equal(g.pivot_x_px, ax) && nearly_equal(g.pivot_y_px, ay),
               "shared anchor");
        // Layout point (ax, ay) as glyph-local (ax - base_x, ay - base_y).
        // Recover base from identity placement: origin under identity tracking.
      }
      // Glyph 1 untransformed base is 10; local offset to anchor is (5, 0).
      const Vec3 mapped = transform_point((*out)[1].matrix, Vec3{5, 0, 0});
      expect(nearly_equal(mapped, Vec3{ax, ay, 0}), "anchor fixed under zoom");
      // A point to the right of the anchor moves farther right.
      const Vec3 right = transform_point((*out)[1].matrix, Vec3{9, 0, 0});
      expect(right.x > ax, "zoom expands from anchor");
    }
  }

  // Interpolated scale (depends on stable numeric easing from motion core):
  // mid-scale still keeps the same pivot as endpoints — no jump.
  {
    GlyphTransformSpec a;
    a.scale_x = 1.0;
    a.scale_y = 1.0;
    a.pivot = GlyphPivotMode::glyph_center;
    GlyphTransformSpec b = a;
    b.scale_x = 2.0;
    b.scale_y = 2.0;
    // Unit easing t=0.5 with cubic ease-out style (1-(1-t)^3) from motion core
    // contract — applied to the scale parameter only.
    const double t = 0.5;
    const double u = 1.0 - t;
    const double eased = 1.0 - u * u * u;
    GlyphTransformSpec mid = a;
    mid.scale_x = a.scale_x + (b.scale_x - a.scale_x) * eased;
    mid.scale_y = a.scale_y + (b.scale_y - a.scale_y) * eased;
    auto sa = apply_glyph_transforms(*layout, shaped, a);
    auto sm = apply_glyph_transforms(*layout, shaped, mid);
    auto sb = apply_glyph_transforms(*layout, shaped, b);
    expect(sa && sm && sb, "lerp samples");
    if (sa && sm && sb) {
      expect(nearly_equal((*sa)[0].pivot_x_px, (*sm)[0].pivot_x_px) &&
                 nearly_equal((*sm)[0].pivot_x_px, (*sb)[0].pivot_x_px),
             "pivot stable across scale animation");
      const double adv =
          static_cast<double>(shaped.glyphs[0].x_advance_26_6) / 64.0;
      const Vec3 pm = transform_point((*sm)[0].matrix, Vec3{adv * 0.5, 0, 0});
      expect(nearly_equal(pm.x, (*sm)[0].pivot_x_px), "mid keeps pivot");
    }
  }

  // Error paths.
  {
    GlyphTransformSpec bad = make_anchor_preserving_zoom(0.0, 0, 0);
    expect(!apply_glyph_transforms(*layout, shaped, bad), "reject zero scale");
    bad = GlyphTransformSpec{};
    bad.scale_x = std::numeric_limits<double>::quiet_NaN();
    expect(!apply_glyph_transforms(*layout, shaped, bad), "reject NaN");
    ShapedText mismatch = shaped;
    mismatch.glyphs.pop_back();
    expect(!apply_glyph_transforms(*layout, mismatch, GlyphTransformSpec{}),
           "reject mismatch");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
