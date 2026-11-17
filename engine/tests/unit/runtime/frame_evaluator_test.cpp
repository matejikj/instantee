#include <instantee/core_model/color.hpp>
#include <instantee/core_model/orientation.hpp>
#include <instantee/runtime/frame_evaluator.hpp>

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

void expect_near(double actual, double expected, double tol,
                 const char *message) {
  if (!(std::isfinite(actual) && std::abs(actual - expected) <= tol)) {
    std::cerr << "FAIL: " << message << " got " << actual << " expected "
              << expected << '\n';
    ++failures;
  }
}

bool same_bits(double a, double b) {
  return std::memcmp(&a, &b, sizeof(double)) == 0;
}

} // namespace

int main() {
  using namespace instantee::core;
  using namespace instantee::runtime;
  constexpr double kPi = std::numbers::pi_v<double>;

  CompiledSceneDraft draft;
  draft.canvas_width = 1080;
  draft.canvas_height = 1920;
  draft.duration_frames = FrameIndex{60};
  draft.seed = 42;
  draft.node_ids = {"root", "child"};

  RuntimeNode root;
  root.kind = RuntimeNodeKind::group;
  root.transform.translate_x = 10;

  RuntimeNode child;
  child.kind = RuntimeNodeKind::text;
  child.parent_index = 0;
  child.transform.translate_y = 5;
  child.transform.opacity = 1.0;
  child.blend = BlendMode::premultiplied_alpha;

  draft.nodes = {root, child};
  auto scene = make_compiled_scene(std::move(draft));
  expect(scene.has_value(), "compiled scene");
  if (!scene) {
    return EXIT_FAILURE;
  }

  AnimationProgram program;

  auto opacity = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, 0.0},
          ScalarKeyframe{FrameIndex{10}, 1.0},
      },
      TrackInterpolation::linear);
  expect(opacity.has_value(), "opacity track");
  program.scalar_bindings.push_back(ScalarPropertyBinding{
      .node_index = 1,
      .channel = AnimatableChannel::opacity,
      .track = *opacity,
  });

  // Rotation across ±π must use short-arc, not scalar value lerp.
  auto rotation = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, kPi - 0.1},
          ScalarKeyframe{FrameIndex{10}, -kPi + 0.1},
      },
      TrackInterpolation::linear);
  expect(rotation.has_value(), "rotation track");
  program.scalar_bindings.push_back(ScalarPropertyBinding{
      .node_index = 1,
      .channel = AnimatableChannel::rotation_z_rad,
      .track = *rotation,
  });

  auto spring = make_spring_preset(10.0, 1.0);
  expect(spring.has_value(), "spring preset");
  program.spring_bindings.push_back(SpringPropertyBinding{
      .node_index = 1,
      .channel = AnimatableChannel::translate_x,
      .preset = *spring,
      .from = 0.0,
      .to = 4.0,
      .initial_velocity = 0.0,
  });

  auto color = make_color_track(
      {
          ColorKeyframe{FrameIndex{0}, srgb_rgba_to_linear(0, 0, 0, 1)},
          ColorKeyframe{FrameIndex{10}, srgb_rgba_to_linear(1, 1, 1, 1)},
      },
      TrackInterpolation::linear);
  expect(color.has_value(), "color track");
  program.color_bindings.push_back(ColorPropertyBinding{
      .node_index = 1,
      .track = *color,
  });

  // Random-access order independence (bit-identical FrameState fields).
  {
    std::vector<FrameState> reference(11);
    for (std::uint64_t f = 0; f <= 10; ++f) {
      auto state = evaluate_frame(**scene, program, FrameIndex{f});
      expect(state.has_value(), "reference evaluate");
      if (state) {
        reference[static_cast<std::size_t>(f)] = std::move(*state);
      }
    }
    const std::vector<std::uint64_t> order = {7, 0, 10, 3, 1, 5};
    for (std::uint64_t f : order) {
      auto state = evaluate_frame(**scene, program, FrameIndex{f});
      expect(state.has_value(), "permuted evaluate");
      if (!state) {
        continue;
      }
      const FrameState &ref = reference[static_cast<std::size_t>(f)];
      expect(state->seed == ref.seed, "seed stable");
      expect(
          same_bits(state->nodes[1].local.opacity, ref.nodes[1].local.opacity),
          "opacity bit-identical");
      expect(same_bits(state->nodes[1].local.rotation_z_rad,
                       ref.nodes[1].local.rotation_z_rad),
             "rotation bit-identical");
      expect(same_bits(state->nodes[1].local.translate_x,
                       ref.nodes[1].local.translate_x),
             "spring translate bit-identical");
      expect(same_bits(state->nodes[1].material_color.r,
                       ref.nodes[1].material_color.r),
             "color bit-identical");
      expect(same_bits(state->nodes[1].world(0, 3), ref.nodes[1].world(0, 3)),
             "world tx bit-identical");
    }
  }

  // Short-arc midpoint near ±π (not the long way through 0).
  {
    auto mid = evaluate_frame(**scene, program, FrameIndex{5});
    expect(mid.has_value(), "mid frame");
    if (mid) {
      expect_near(wrap_angle_pi(mid->nodes[1].local.rotation_z_rad),
                  wrap_angle_pi(kPi), 1e-9, "rotation short-arc mid");
      expect_near(mid->nodes[1].local.opacity, 0.5, 1e-12, "opacity mid");
      expect_near(mid->nodes[1].material_color.r, 0.5, 1e-12,
                  "linear color mid");
      // Parent translate_x=10 plus child spring sample.
      expect(mid->nodes[1].world(0, 3) > 10.0,
             "world includes parent + spring");
      expect(!mid->draw_order.transparent_paint.empty() ||
                 !mid->draw_order.opaque_batch.empty(),
             "draw order populated");
    }
  }

  // Ad hoc float lerp is not a substitute: rotation track mid must differ from
  // naive (from+to)/2 which crosses the long arc.
  {
    const double naive = ((kPi - 0.1) + (-kPi + 0.1)) * 0.5; // ≈ 0
    auto mid = evaluate_frame(**scene, program, FrameIndex{5});
    if (mid) {
      expect(std::abs(mid->nodes[1].local.rotation_z_rad - naive) > 1.0,
             "short-arc differs from naive float mean");
    }
  }

  // Out-of-range binding fails without silently inventing a lerp.
  {
    AnimationProgram bad = program;
    bad.scalar_bindings.push_back(ScalarPropertyBinding{
        .node_index = 99,
        .channel = AnimatableChannel::opacity,
        .track = *opacity,
    });
    expect(!evaluate_frame(**scene, bad, FrameIndex{0}).has_value(),
           "reject dangling binding");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
