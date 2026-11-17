#include <instantee/direction/creative_variation.hpp>
#include <instantee/direction/motion_grammar.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using instantee::direction::apply_creative_variation;
using instantee::direction::CreativeVariation;
using instantee::direction::derive_object_subseed;
using instantee::direction::derive_shot_subseed;
using instantee::direction::GesturePattern;
using instantee::direction::IntentKind;
using instantee::direction::MotionEase;
using instantee::direction::MotionGrammarBinding;
using instantee::direction::sample_creative_variation;
using instantee::direction::sample_variation_unit;
using instantee::direction::VariationIdentity;
using instantee::direction::VariationSafeIntervals;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_bits_equal(double a, double b, const char *message) {
  expect(std::memcmp(&a, &b, sizeof(double)) == 0, message);
}

[[nodiscard]] VariationIdentity make_id(std::string_view shot,
                                        std::string_view object) {
  VariationIdentity id;
  id.film_seed = 0xC0FFEEULL;
  id.package_id = "editorial";
  id.package_version = "1.0.0";
  id.shot_id = std::string(shot);
  id.object_id = std::string(object);
  return id;
}

} // namespace

int main() {
  // Same identity → bit-identical variation (call-order independent).
  {
    auto a = sample_creative_variation(make_id("intro", "headline"));
    auto b = sample_creative_variation(make_id("intro", "headline"));
    expect(a.has_value() && b.has_value(), "sample twice");
    if (a && b) {
      expect(a->shot_subseed == b->shot_subseed, "shot subseed stable");
      expect(a->object_subseed == b->object_subseed, "object subseed stable");
      expect_bits_equal(a->intensity_scale, b->intensity_scale, "intensity");
      expect_bits_equal(a->duration_scale, b->duration_scale, "duration");
      expect_bits_equal(a->stagger_scale, b->stagger_scale, "stagger");
    }
  }

  // Adding an unrelated shot must not change an existing object's variation.
  {
    auto before = sample_creative_variation(make_id("intro", "headline"));
    expect(before.has_value(), "baseline variation");

    // Simulate a larger film: derive many unrelated shot/object streams first.
    double sink = 0.0;
    for (int i = 0; i < 32; ++i) {
      const std::string shot = "extra_" + std::to_string(i);
      const std::string object = "deco_" + std::to_string(i);
      auto extra = sample_creative_variation(make_id(shot, object));
      expect(extra.has_value(), "extra variation");
      if (extra) {
        sink += extra->intensity_scale + extra->duration_scale +
                extra->stagger_scale;
      }
    }

    auto after = sample_creative_variation(make_id("intro", "headline"));
    expect(after.has_value(), "variation after unrelated shots");
    if (before && after) {
      expect(before->shot_subseed == after->shot_subseed,
             "unrelated shot leaves shot subseed unchanged");
      expect(before->object_subseed == after->object_subseed,
             "unrelated shot leaves object subseed unchanged");
      expect_bits_equal(before->intensity_scale, after->intensity_scale,
                        "intensity unchanged by unrelated shot");
      expect_bits_equal(before->duration_scale, after->duration_scale,
                        "duration unchanged by unrelated shot");
      expect_bits_equal(before->stagger_scale, after->stagger_scale,
                        "stagger unchanged by unrelated shot");
    }
    expect(std::isfinite(sink), "sink used");
  }

  // Style Bible version participates in the mix; package revision changes
  // variation intentionally.
  {
    auto v1 = sample_creative_variation(make_id("intro", "headline"));
    VariationIdentity next = make_id("intro", "headline");
    next.package_version = "1.0.1";
    auto v2 = sample_creative_variation(next);
    expect(v1.has_value() && v2.has_value(), "versioned samples");
    if (v1 && v2) {
      expect(v1->shot_subseed != v2->shot_subseed,
             "style version changes shot subseed");
    }
  }

  // Safe intervals clamp draws.
  {
    VariationSafeIntervals tight;
    tight.intensity_min = 0.9;
    tight.intensity_max = 1.1;
    tight.duration_scale_min = 0.95;
    tight.duration_scale_max = 1.05;
    tight.stagger_scale_min = 0.25;
    tight.stagger_scale_max = 0.75;
    for (int i = 0; i < 16; ++i) {
      auto sample = sample_creative_variation(
          make_id("s" + std::to_string(i), "o" + std::to_string(i)), tight);
      expect(sample.has_value(), "tight interval sample");
      if (!sample) {
        continue;
      }
      expect(sample->intensity_scale >= tight.intensity_min &&
                 sample->intensity_scale <= tight.intensity_max,
             "intensity in safe interval");
      expect(sample->duration_scale >= tight.duration_scale_min &&
                 sample->duration_scale <= tight.duration_scale_max,
             "duration in safe interval");
      expect(sample->stagger_scale >= tight.stagger_scale_min &&
                 sample->stagger_scale <= tight.stagger_scale_max,
             "stagger in safe interval");
    }
  }

  // apply_creative_variation scales grammar binding deterministically.
  {
    MotionGrammarBinding binding{.package_id = "editorial",
                                 .intent = IntentKind::compare,
                                 .pattern = GesturePattern::stagger,
                                 .ease = MotionEase::ease_out,
                                 .duration_scale = 0.5,
                                 .intensity_scale = 1.0,
                                 .stagger_frames = 4,
                                 .property = "opacity"};
    auto variation = sample_creative_variation(make_id("intro", "a"));
    expect(variation.has_value(), "variation for apply");
    if (variation) {
      auto a = apply_creative_variation(binding, *variation);
      auto b = apply_creative_variation(binding, *variation);
      expect(a.has_value() && b.has_value(), "apply twice");
      if (a && b) {
        expect_bits_equal(a->intensity_scale, b->intensity_scale,
                          "applied intensity deterministic");
        expect(a->stagger_frames == b->stagger_frames, "applied stagger");
        expect(a->duration_scale >= 0.05 && a->duration_scale <= 1.0,
               "duration scale clamped");
      }
    }
  }

  // Channel partitioning and rejection paths.
  {
    auto shot = derive_shot_subseed(1, "editorial", "1.0.0", "intro");
    expect(shot.has_value(), "shot subseed");
    if (shot) {
      auto object = derive_object_subseed(*shot, "headline");
      expect(object.has_value(), "object subseed");
      if (object) {
        auto u0 = sample_variation_unit(*object, "intensity_scale", 0.0, 1.0);
        auto u1 = sample_variation_unit(*object, "duration_scale", 0.0, 1.0);
        expect(u0.has_value() && u1.has_value(), "channel samples");
        if (u0 && u1) {
          expect(*u0 != *u1, "distinct channels diverge");
        }
      }
    }
    expect(!derive_shot_subseed(1, "", "1.0.0", "intro").has_value(),
           "empty package rejected");
    expect(!derive_object_subseed(1, "").has_value(), "empty object rejected");
    VariationSafeIntervals bad;
    bad.intensity_min = 2.0;
    bad.intensity_max = 1.0;
    expect(!sample_creative_variation(make_id("a", "b"), bad).has_value(),
           "inverted interval rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
