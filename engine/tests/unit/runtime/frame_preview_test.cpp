#include <instantee/runtime/frame_preview.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
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

  CompiledSceneDraft draft;
  draft.canvas_width = 1080;
  draft.canvas_height = 1920;
  draft.duration_frames = FrameIndex{60};
  draft.seed = 9;
  draft.node_ids = {"root", "leaf"};

  RuntimeNode root;
  root.kind = RuntimeNodeKind::group;

  RuntimeNode leaf;
  leaf.kind = RuntimeNodeKind::geometry;
  leaf.parent_index = 0;
  leaf.transform.opacity = 0.0;
  leaf.blend = BlendMode::premultiplied_alpha;

  draft.nodes = {root, leaf};
  auto scene = make_compiled_scene(std::move(draft));
  expect(scene.has_value(), "compiled scene");
  if (!scene) {
    return EXIT_FAILURE;
  }

  AnimationProgram program;
  auto opacity = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, 0.0},
          ScalarKeyframe{FrameIndex{40}, 1.0},
      },
      TrackInterpolation::linear);
  expect(opacity.has_value(), "opacity track");
  program.scalar_bindings.push_back(ScalarPropertyBinding{
      .node_index = 1,
      .channel = AnimatableChannel::opacity,
      .track = *opacity,
  });

  // Single-call seek equals a sequential reference at the same index.
  std::vector<FrameState> sequential;
  sequential.reserve(41);
  for (std::uint64_t f = 0; f <= 40; ++f) {
    auto state = evaluate_frame(**scene, program, FrameIndex{f});
    expect(state.has_value(), "sequential evaluate");
    if (state) {
      sequential.push_back(std::move(*state));
    }
  }

  auto mid = preview_frame_at(**scene, program, FrameIndex{20});
  expect(mid.has_value(), "preview mid");
  if (mid) {
    expect(mid->evaluations == 1, "exactly one evaluation");
    expect(mid->state.frame == FrameIndex{20}, "preview frame index");
    expect(same_bits(mid->state.nodes[1].local.opacity,
                     sequential[20].nodes[1].local.opacity),
           "seek matches sequential opacity");
  }

  // Order independence: late then early without replaying the gap.
  auto late = preview_frame_at(**scene, program, FrameIndex{40});
  auto early = preview_frame_at(**scene, program, FrameIndex{0});
  expect(late.has_value() && early.has_value(), "unordered seeks");
  if (late && early) {
    expect(late->evaluations == 1 && early->evaluations == 1,
           "each seek is one evaluation");
    expect(same_bits(late->state.nodes[1].local.opacity,
                     sequential[40].nodes[1].local.opacity),
           "late seek matches");
    expect(same_bits(early->state.nodes[1].local.opacity,
                     sequential[0].nodes[1].local.opacity),
           "early seek matches");
  }

  auto last = preview_frame_at(**scene, program, FrameIndex{59});
  expect(last.has_value(), "last in-range frame");

  auto oob = preview_frame_at(**scene, program, FrameIndex{60});
  expect(!oob.has_value(), "reject duration boundary");
  if (!oob) {
    expect(oob.error().code == "frame_out_of_range", "out_of_range code");
    expect(oob.error().context.frame.has_value() &&
               *oob.error().context.frame == 60,
           "out_of_range carries frame");
  }

  auto far = preview_frame_at(**scene, program, FrameIndex{1000});
  expect(!far.has_value(), "reject far out of range");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
