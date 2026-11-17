// Determinism phase gate: bit-identical logical FrameState for the same inputs
// and build configuration, independent of evaluation order. Covers random
// access, duration boundaries, structured error paths, and time-conversion
// policy.

#include <instantee/core_model/color.hpp>
#include <instantee/core_model/orientation.hpp>
#include <instantee/core_model/time_convert.hpp>
#include <instantee/runtime/clip_mask.hpp>
#include <instantee/runtime/frame_evaluator.hpp>
#include <instantee/runtime/frame_preview.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numbers>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool same_bits(double a, double b) {
  return std::memcmp(&a, &b, sizeof(double)) == 0;
}

[[nodiscard]] bool same_bits_mat4(const instantee::core::Mat4 &a,
                                  const instantee::core::Mat4 &b) {
  for (std::size_t i = 0; i < a.m.size(); ++i) {
    if (!same_bits(a.m[i], b.m[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool
same_bits_transform(const instantee::runtime::RuntimeTransform &a,
                    const instantee::runtime::RuntimeTransform &b) {
  return same_bits(a.translate_x, b.translate_x) &&
         same_bits(a.translate_y, b.translate_y) &&
         same_bits(a.translate_z, b.translate_z) &&
         same_bits(a.scale_x, b.scale_x) && same_bits(a.scale_y, b.scale_y) &&
         same_bits(a.scale_z, b.scale_z) &&
         same_bits(a.rotation_z_rad, b.rotation_z_rad) &&
         same_bits(a.opacity, b.opacity) && same_bits(a.pivot_x, b.pivot_x) &&
         same_bits(a.pivot_y, b.pivot_y) && same_bits(a.pivot_z, b.pivot_z);
}

[[nodiscard]] bool same_bits_color(const instantee::core::LinearRgba &a,
                                   const instantee::core::LinearRgba &b) {
  return same_bits(a.r, b.r) && same_bits(a.g, b.g) && same_bits(a.b, b.b) &&
         same_bits(a.a, b.a);
}

[[nodiscard]] bool
same_bits_clip(const std::optional<instantee::runtime::ClipRect> &a,
               const std::optional<instantee::runtime::ClipRect> &b) {
  if (a.has_value() != b.has_value()) {
    return false;
  }
  if (!a) {
    return true;
  }
  return same_bits(a->min_x, b->min_x) && same_bits(a->min_y, b->min_y) &&
         same_bits(a->max_x, b->max_x) && same_bits(a->max_y, b->max_y);
}

[[nodiscard]] bool same_bits_draw_item(const instantee::runtime::DrawItem &a,
                                       const instantee::runtime::DrawItem &b) {
  return a.node_index == b.node_index && a.blend == b.blend &&
         same_bits(a.opacity, b.opacity) && a.layer_order == b.layer_order &&
         same_bits(a.depth_z, b.depth_z) && a.node_id == b.node_id;
}

[[nodiscard]] bool
same_bits_draw_order(const instantee::runtime::DrawOrder &a,
                     const instantee::runtime::DrawOrder &b) {
  if (a.opaque_batch.size() != b.opaque_batch.size() ||
      a.transparent_paint.size() != b.transparent_paint.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.opaque_batch.size(); ++i) {
    if (!same_bits_draw_item(a.opaque_batch[i], b.opaque_batch[i])) {
      return false;
    }
  }
  for (std::size_t i = 0; i < a.transparent_paint.size(); ++i) {
    if (!same_bits_draw_item(a.transparent_paint[i], b.transparent_paint[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool same_bits_node(const instantee::runtime::NodeFrameState &a,
                                  const instantee::runtime::NodeFrameState &b) {
  return same_bits_transform(a.local, b.local) &&
         same_bits_mat4(a.world, b.world) && a.visible == b.visible &&
         a.inherited_visible == b.inherited_visible &&
         same_bits_clip(a.world_clip, b.world_clip) &&
         a.clip_empty == b.clip_empty &&
         a.mask_node_index == b.mask_node_index &&
         same_bits_color(a.material_color, b.material_color);
}

[[nodiscard]] bool same_bits_frame(const instantee::runtime::FrameState &a,
                                   const instantee::runtime::FrameState &b) {
  if (a.frame != b.frame || a.seed != b.seed ||
      a.nodes.size() != b.nodes.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.nodes.size(); ++i) {
    if (!same_bits_node(a.nodes[i], b.nodes[i])) {
      return false;
    }
  }
  return same_bits_draw_order(a.draw_order, b.draw_order);
}

} // namespace

int main() {
  using namespace instantee::core;
  using namespace instantee::runtime;
  constexpr double kPi = std::numbers::pi_v<double>;

  CompiledSceneDraft draft;
  draft.canvas_width = 1080;
  draft.canvas_height = 1920;
  draft.duration_frames = FrameIndex{48};
  draft.seed = 0xC0FFEEu;
  draft.node_ids = {"root", "masked", "leaf"};

  RuntimeNode root;
  root.kind = RuntimeNodeKind::group;
  root.transform.translate_x = 10.0;

  RuntimeNode masked;
  masked.kind = RuntimeNodeKind::group;
  masked.parent_index = 0;
  masked.transform.translate_y = 5.0;

  RuntimeNode leaf;
  leaf.kind = RuntimeNodeKind::geometry;
  leaf.parent_index = 1;
  leaf.blend = BlendMode::premultiplied_alpha;
  leaf.transform.opacity = 0.0;

  draft.nodes = {root, masked, leaf};
  auto scene = make_compiled_scene(std::move(draft));
  expect(scene.has_value(), "compiled scene");
  if (!scene) {
    return EXIT_FAILURE;
  }

  AnimationProgram program;

  auto opacity = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, 0.0},
          ScalarKeyframe{FrameIndex{24}, 1.0},
          ScalarKeyframe{FrameIndex{47}, 0.25},
      },
      TrackInterpolation::linear);
  expect(opacity.has_value(), "opacity track");
  program.scalar_bindings.push_back(ScalarPropertyBinding{
      .node_index = 2,
      .channel = AnimatableChannel::opacity,
      .track = *opacity,
  });

  auto rotation = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, kPi - 0.1},
          ScalarKeyframe{FrameIndex{24}, -kPi + 0.1},
      },
      TrackInterpolation::linear);
  expect(rotation.has_value(), "rotation track");
  program.scalar_bindings.push_back(ScalarPropertyBinding{
      .node_index = 2,
      .channel = AnimatableChannel::rotation_z_rad,
      .track = *rotation,
  });

  auto spring = make_spring_preset(12.0, 0.75);
  expect(spring.has_value(), "spring preset");
  program.spring_bindings.push_back(SpringPropertyBinding{
      .node_index = 2,
      .channel = AnimatableChannel::translate_x,
      .preset = *spring,
      .from = 0.0,
      .to = 8.0,
      .initial_velocity = 0.0,
  });

  auto color = make_color_track(
      {
          ColorKeyframe{FrameIndex{0}, srgb_rgba_to_linear(0, 0, 0, 1)},
          ColorKeyframe{FrameIndex{24}, srgb_rgba_to_linear(1, 0.5, 0, 1)},
      },
      TrackInterpolation::linear);
  expect(color.has_value(), "color track");
  program.color_bindings.push_back(ColorPropertyBinding{
      .node_index = 2,
      .track = *color,
  });

  ClipMaskProgram clips;
  clips.bindings.push_back(NodeClipMaskBinding{
      .node_index = 1,
      .local_clip =
          ClipRect{.min_x = -100, .min_y = -100, .max_x = 100, .max_y = 100},
      .mask_node_index = std::nullopt,
  });
  clips.bindings.push_back(NodeClipMaskBinding{
      .node_index = 2,
      .local_clip = std::nullopt,
      .mask_node_index = 1,
  });
  expect(validate_clip_mask_program(**scene, clips).has_value(),
         "clip program valid");

  // --- Random-access order independence (full FrameState bit identity) ---
  const std::uint64_t duration = (**scene).duration_frames().value();
  std::vector<FrameState> reference(static_cast<std::size_t>(duration));
  for (std::uint64_t f = 0; f < duration; ++f) {
    auto state = evaluate_frame(**scene, program, clips, FrameIndex{f});
    expect(state.has_value(), "sequential evaluate");
    if (state) {
      reference[static_cast<std::size_t>(f)] = std::move(*state);
    }
  }

  const std::vector<std::uint64_t> permute = {0, 47, 23, 1,  40, 12,
                                              7, 33, 2,  46, 24, 5};
  for (std::uint64_t f : permute) {
    auto state = evaluate_frame(**scene, program, clips, FrameIndex{f});
    expect(state.has_value(), "permuted evaluate");
    if (!state) {
      continue;
    }
    expect(same_bits_frame(*state, reference[static_cast<std::size_t>(f)]),
           "permuted FrameState bit-identical to sequential");
  }

  // Preview path: single evaluation, bit-identical to evaluate_frame.
  for (std::uint64_t f : {0ULL, 24ULL, 47ULL}) {
    auto preview = preview_frame_at(**scene, program, clips, FrameIndex{f});
    expect(preview.has_value(), "preview in-range");
    if (!preview) {
      continue;
    }
    expect(preview->evaluations == 1, "preview is one evaluation");
    expect(
        same_bits_frame(preview->state, reference[static_cast<std::size_t>(f)]),
        "preview matches evaluate_frame");
  }

  // --- Boundaries ---
  expect(preview_frame_at(**scene, program, clips, FrameIndex{0}).has_value(),
         "frame 0 accepted");
  expect(preview_frame_at(**scene, program, clips, FrameIndex{duration - 1})
             .has_value(),
         "last frame accepted");
  auto oob = preview_frame_at(**scene, program, clips, FrameIndex{duration});
  expect(!oob.has_value(), "duration boundary rejected");
  if (!oob) {
    expect(oob.error().code == "frame_out_of_range", "out_of_range code");
  }

  // --- Error paths (no silent invention) ---
  {
    AnimationProgram bad = program;
    bad.scalar_bindings.push_back(ScalarPropertyBinding{
        .node_index = 99,
        .channel = AnimatableChannel::opacity,
        .track = *opacity,
    });
    expect(!evaluate_frame(**scene, bad, clips, FrameIndex{0}).has_value(),
           "reject dangling binding");
  }
  {
    ClipMaskProgram bad_clips = clips;
    bad_clips.bindings.push_back(NodeClipMaskBinding{
        .node_index = 0,
        .local_clip = ClipRect{.min_x = 1, .min_y = 1, .max_x = 0, .max_y = 0},
        .mask_node_index = std::nullopt,
    });
    expect(!validate_clip_mask_program(**scene, bad_clips).has_value(),
           "reject denormalized clip");
  }

  // --- Time conversion policy (nearest / half-away, exclusive intervals) ---
  {
    const auto rate = FrameRate::media_30_1();
    auto one = RationalTime::make(1, 1);
    expect(one.has_value(), "rational 1s");
    if (one) {
      auto frame = seconds_to_frame(*one, rate);
      expect(frame.has_value() && frame->frame.value() == 30ULL,
             "1s → frame 30");
      expect(frame && !frame->sub_frame_loss, "exact second no loss");
    }
    auto half_frame = RationalTime::make(1, 60);
    expect(half_frame.has_value(), "rational 1/60");
    if (half_frame) {
      auto frame = seconds_to_frame(*half_frame, rate);
      expect(frame.has_value() && frame->frame.value() == 1ULL,
             "half-away 0.5 frames → 1");
      expect(frame && frame->sub_frame_loss, "sub-frame loss flagged");
    }
    auto start = RationalTime::make(1, 2);
    auto end = RationalTime::make(1, 1);
    expect(start.has_value() && end.has_value(), "interval rationals");
    if (start && end) {
      auto interval = seconds_interval_to_frames(*start, *end, rate);
      expect(interval.has_value(), "interval conversion");
      if (interval) {
        expect(interval->start.value() == 15ULL, "interval start");
        expect(interval->end_exclusive.value() == 30ULL,
               "interval end exclusive");
      }
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "determinism_phase_gate: ok\n";
  return EXIT_SUCCESS;
}
