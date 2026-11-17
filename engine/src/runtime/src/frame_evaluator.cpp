#include <instantee/runtime/frame_evaluator.hpp>

#include <instantee/core_model/orientation.hpp>
#include <instantee/runtime/easing.hpp>
#include <instantee/runtime/transforms.hpp>

#include <cmath>
#include <string_view>
#include <utility>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

double unit_progress(core::FrameIndex frame, core::FrameIndex left,
                     core::FrameIndex right) noexcept {
  const double span =
      static_cast<double>(right.value()) - static_cast<double>(left.value());
  const double delta =
      static_cast<double>(frame.value()) - static_cast<double>(left.value());
  return delta / span;
}

std::expected<double, diagnostics::Error>
eased_unit(const ScalarTrack &track, const TrackSegment &segment,
           core::FrameIndex frame) {
  if (!segment.right.has_value()) {
    return 0.0;
  }
  if (frame == segment.left.frame) {
    return 0.0;
  }
  if (track.interpolation == TrackInterpolation::hold) {
    return 0.0;
  }
  const double u =
      unit_progress(frame, segment.left.frame, segment.right->frame);
  if (track.interpolation == TrackInterpolation::linear) {
    return u;
  }
  if (track.interpolation == TrackInterpolation::cubic_bezier) {
    if (!track.bezier.has_value()) {
      return std::unexpected(
          err("missing_bezier_controls",
              "cubic_bezier interpolation requires control points"));
    }
    return sample_cubic_bezier(*track.bezier, u);
  }
  return std::unexpected(
      err("unsupported_interpolation",
          "Frame evaluator has no sampler for this interpolation kind"));
}

// Rotation must use short-arc angle policy; scalar track value lerp is wrong
// across ±π and is intentionally not used here.
std::expected<double, diagnostics::Error>
sample_rotation_z_track(const ScalarTrack &track, core::FrameIndex frame) {
  auto segment = find_track_segment(track, frame);
  if (!segment) {
    return std::unexpected(segment.error());
  }
  if (!segment->right.has_value()) {
    return segment->left.value;
  }
  if (track.interpolation == TrackInterpolation::hold ||
      frame == segment->left.frame) {
    return segment->left.value;
  }
  auto u = eased_unit(track, *segment, frame);
  if (!u) {
    return std::unexpected(u.error());
  }
  return core::lerp_angle_short(segment->left.value, segment->right->value, *u);
}

std::expected<double, diagnostics::Error>
sample_scalar_channel(const ScalarTrack &track, AnimatableChannel channel,
                      core::FrameIndex frame) {
  if (channel == AnimatableChannel::rotation_z_rad) {
    return sample_rotation_z_track(track, frame);
  }
  return evaluate_scalar_track(track, frame);
}

void apply_channel(RuntimeTransform &transform, AnimatableChannel channel,
                   double value) noexcept {
  switch (channel) {
  case AnimatableChannel::translate_x:
    transform.translate_x = value;
    break;
  case AnimatableChannel::translate_y:
    transform.translate_y = value;
    break;
  case AnimatableChannel::translate_z:
    transform.translate_z = value;
    break;
  case AnimatableChannel::scale_x:
    transform.scale_x = value;
    break;
  case AnimatableChannel::scale_y:
    transform.scale_y = value;
    break;
  case AnimatableChannel::scale_z:
    transform.scale_z = value;
    break;
  case AnimatableChannel::rotation_z_rad:
    transform.rotation_z_rad = value;
    break;
  case AnimatableChannel::opacity:
    transform.opacity = value;
    break;
  }
}

std::expected<double, diagnostics::Error>
eased_color_unit(const ColorTrack &track, const ColorKeyframe &left,
                 const ColorKeyframe &right, core::FrameIndex frame) {
  if (frame == left.frame) {
    return 0.0;
  }
  if (track.interpolation == TrackInterpolation::hold) {
    return 0.0;
  }
  const double u = unit_progress(frame, left.frame, right.frame);
  if (track.interpolation == TrackInterpolation::linear) {
    return u;
  }
  if (track.interpolation == TrackInterpolation::cubic_bezier) {
    if (!track.bezier.has_value()) {
      return std::unexpected(
          err("missing_bezier_controls",
              "cubic_bezier interpolation requires control points"));
    }
    return sample_cubic_bezier(*track.bezier, u);
  }
  return std::unexpected(
      err("unsupported_interpolation",
          "Frame evaluator has no sampler for this interpolation kind"));
}

} // namespace

std::expected<ColorTrack, diagnostics::Error> make_color_track(
    std::vector<ColorKeyframe> keyframes, TrackInterpolation interpolation,
    TrackEndBehavior end_behavior, std::optional<CubicBezierControls> bezier) {
  if (keyframes.empty()) {
    return std::unexpected(
        err("empty_track", "Color track requires at least one keyframe"));
  }
  for (std::size_t index = 0; index < keyframes.size(); ++index) {
    if (!core::finite_linear_rgba(keyframes[index].value)) {
      return std::unexpected(
          err("non_finite_value", "Color keyframe must be finite"));
    }
    if (index > 0 && keyframes[index].frame <= keyframes[index - 1].frame) {
      return std::unexpected(
          err("non_increasing_keyframes",
              "Color keyframes must be strictly increasing by frame"));
    }
  }
  if (interpolation == TrackInterpolation::cubic_bezier) {
    if (!bezier.has_value()) {
      return std::unexpected(
          err("missing_bezier_controls",
              "cubic_bezier interpolation requires control points"));
    }
  } else if (bezier.has_value()) {
    return std::unexpected(
        err("unexpected_bezier_controls",
            "Bezier controls are only valid with cubic_bezier interpolation"));
  }
  ColorTrack track;
  track.keyframes = std::move(keyframes);
  track.interpolation = interpolation;
  track.end_behavior = end_behavior;
  track.bezier = bezier;
  return track;
}

std::expected<core::LinearRgba, diagnostics::Error>
evaluate_color_track(const ColorTrack &track, core::FrameIndex frame) {
  if (track.keyframes.empty()) {
    return std::unexpected(
        err("empty_track", "Color track requires at least one keyframe"));
  }
  const auto &keys = track.keyframes;
  // Color end policy is clamp (linear-RGBA extrapolation is not defined as a
  // separate working-space mode in this evaluator).
  (void)track.end_behavior;
  if (frame < keys.front().frame) {
    return keys.front().value;
  }
  if (frame >= keys.back().frame) {
    return keys.back().value;
  }

  std::size_t lo = 0;
  std::size_t hi = keys.size() - 1;
  while (lo + 1 < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (keys[mid].frame <= frame) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  const ColorKeyframe &left = keys[lo];
  const ColorKeyframe &right = keys[hi];
  auto u = eased_color_unit(track, left, right, frame);
  if (!u) {
    return std::unexpected(u.error());
  }
  return core::lerp_linear_rgba(left.value, right.value, *u);
}

std::expected<FrameState, diagnostics::Error>
evaluate_frame(const CompiledScene &scene, const AnimationProgram &program,
               core::FrameIndex frame) {
  const auto base_nodes = scene.nodes();
  const auto node_ids = scene.node_ids();
  if (base_nodes.size() != node_ids.size()) {
    return std::unexpected(
        err("length_mismatch", "node_ids and nodes must match"));
  }

  std::vector<RuntimeNode> locals(base_nodes.begin(), base_nodes.end());
  std::vector<core::LinearRgba> colors(locals.size(),
                                       core::LinearRgba{1.0, 1.0, 1.0, 1.0});

  for (const ScalarPropertyBinding &binding : program.scalar_bindings) {
    if (binding.node_index >= locals.size()) {
      return std::unexpected(
          err("dangling_reference", "Scalar binding node_index out of range"));
    }
    auto value = sample_scalar_channel(binding.track, binding.channel, frame);
    if (!value) {
      return std::unexpected(value.error());
    }
    apply_channel(locals[binding.node_index].transform, binding.channel,
                  *value);
  }

  for (const SpringPropertyBinding &binding : program.spring_bindings) {
    if (binding.node_index >= locals.size()) {
      return std::unexpected(
          err("dangling_reference", "Spring binding node_index out of range"));
    }
    auto value = sample_spring_at_frame(binding.preset, binding.from,
                                        binding.to, binding.initial_velocity,
                                        frame, scene.frame_rate());
    if (!value) {
      return std::unexpected(value.error());
    }
    apply_channel(locals[binding.node_index].transform, binding.channel,
                  *value);
  }

  for (const ColorPropertyBinding &binding : program.color_bindings) {
    if (binding.node_index >= locals.size()) {
      return std::unexpected(
          err("dangling_reference", "Color binding node_index out of range"));
    }
    auto color = evaluate_color_track(binding.track, frame);
    if (!color) {
      return std::unexpected(color.error());
    }
    colors[binding.node_index] = *color;
  }

  auto worlds = compute_world_matrices(locals);
  if (!worlds) {
    return std::unexpected(worlds.error());
  }

  std::vector<double> depth(worlds->size(), 0.0);
  for (std::size_t index = 0; index < worlds->size(); ++index) {
    depth[index] = (*worlds)[index](2, 3);
  }
  auto order = build_draw_order(locals, node_ids, depth);
  if (!order) {
    return std::unexpected(order.error());
  }

  FrameState state;
  state.frame = frame;
  state.seed = scene.seed();
  state.nodes.resize(locals.size());
  for (std::size_t index = 0; index < locals.size(); ++index) {
    NodeFrameState &node = state.nodes[index];
    node.local = locals[index].transform;
    node.world = (*worlds)[index];
    node.visible = locals[index].visible;
    node.material_color = colors[index];
  }
  state.draw_order = std::move(*order);
  return state;
}

} // namespace instantee::runtime
