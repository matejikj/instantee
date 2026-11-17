#pragma once

#include <instantee/core_model/color.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/clip_mask.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/layering.hpp>
#include <instantee/runtime/spring.hpp>
#include <instantee/runtime/timeline.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace instantee::runtime {

// Channels that ScalarTrack / Spring bindings may drive. Rotation uses the
// short-arc angle policy; other channels use typed scalar track sampling.
enum class AnimatableChannel : std::uint8_t {
  translate_x = 0,
  translate_y,
  translate_z,
  scale_x,
  scale_y,
  scale_z,
  rotation_z_rad,
  opacity
};

struct ScalarPropertyBinding {
  std::uint32_t node_index = 0;
  AnimatableChannel channel = AnimatableChannel::opacity;
  ScalarTrack track{};
};

struct SpringPropertyBinding {
  std::uint32_t node_index = 0;
  AnimatableChannel channel = AnimatableChannel::opacity;
  SpringPreset preset{};
  double from = 0.0;
  double to = 1.0;
  double initial_velocity = 0.0;
};

struct ColorKeyframe {
  core::FrameIndex frame{};
  core::LinearRgba value{};
};

// Color tracks interpolate only via lerp_linear_rgba (linear working space).
struct ColorTrack {
  std::vector<ColorKeyframe> keyframes;
  TrackInterpolation interpolation = TrackInterpolation::linear;
  TrackEndBehavior end_behavior = TrackEndBehavior::clamp;
  std::optional<CubicBezierControls> bezier;
};

struct ColorPropertyBinding {
  std::uint32_t node_index = 0;
  ColorTrack track{};
};

// Immutable animation program evaluated with a CompiledScene. Bindings are
// applied in vector order; a later binding to the same channel replaces the
// earlier sample for that frame.
struct AnimationProgram {
  std::vector<ScalarPropertyBinding> scalar_bindings;
  std::vector<SpringPropertyBinding> spring_bindings;
  std::vector<ColorPropertyBinding> color_bindings;
};

struct NodeFrameState {
  RuntimeTransform local{};
  core::Mat4 world = core::Mat4::identity();
  bool visible = true;
  // Hierarchy AND of local visibility flags (self + ancestors).
  bool inherited_visible = true;
  // World-space clip from ancestor chain; nullopt when unconstrained.
  std::optional<ClipRect> world_clip;
  bool clip_empty = false;
  // Nearest inherited visibility-mask node index, if any.
  std::optional<std::uint32_t> mask_node_index;
  core::LinearRgba material_color{1.0, 1.0, 1.0, 1.0};
};

// Logical scene state for one absolute film frame. Independent of evaluation
// order: FrameState(n) does not read FrameState(n-1).
struct FrameState {
  core::FrameIndex frame{};
  std::uint64_t seed = 0;
  std::vector<NodeFrameState> nodes;
  DrawOrder draw_order{};
};

[[nodiscard]] std::expected<ColorTrack, diagnostics::Error>
make_color_track(std::vector<ColorKeyframe> keyframes,
                 TrackInterpolation interpolation = TrackInterpolation::linear,
                 TrackEndBehavior end_behavior = TrackEndBehavior::clamp,
                 std::optional<CubicBezierControls> bezier = std::nullopt);

[[nodiscard]] std::expected<core::LinearRgba, diagnostics::Error>
evaluate_color_track(const ColorTrack &track, core::FrameIndex frame);

// Evaluate CompiledScene + AnimationProgram at `frame`. Uses typed samplers
// only (scalar track / short-arc rotation / spring / linear-RGBA); never an
// ad hoc float lerp for missing interpolation kinds. Without a clip program,
// inherited_visible still reflects the parent visibility chain; clip/mask
// fields stay empty.
[[nodiscard]] std::expected<FrameState, diagnostics::Error>
evaluate_frame(const CompiledScene &scene, const AnimationProgram &program,
               core::FrameIndex frame);

// Same as evaluate_frame, then resolves hierarchical clip regions and
// visibility-mask references into each NodeFrameState.
[[nodiscard]] std::expected<FrameState, diagnostics::Error>
evaluate_frame(const CompiledScene &scene, const AnimationProgram &program,
               const ClipMaskProgram &clips, core::FrameIndex frame);

} // namespace instantee::runtime
