#include <instantee/scene_ir/puppet_secondary.hpp>

#include <instantee/core_model/deterministic_rng.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "puppet_secondary", message};
}

[[nodiscard]] bool finite_positive(double value) noexcept {
  return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool finite_nonneg(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] double clamp01(double value) noexcept {
  if (!std::isfinite(value)) {
    return 0.0;
  }
  if (value < 0.0) {
    return 0.0;
  }
  if (value > 1.0) {
    return 1.0;
  }
  return value;
}

// Raised cosine pulse in [0, 1] over a closed window.
[[nodiscard]] double blink_envelope(double local_s,
                                    double duration_s) noexcept {
  if (!(duration_s > 0.0) || local_s < 0.0 || local_s > duration_s) {
    return 0.0;
  }
  const double u = local_s / duration_s;
  return 0.5 - 0.5 * std::cos(6.283185307179586 * u);
}

[[nodiscard]] double sin01(double phase) noexcept {
  return std::sin(6.283185307179586 * phase);
}

} // namespace

std::expected<void, diagnostics::Error> validate_puppet_secondary_motion_config(
    const PuppetSecondaryMotionConfig &config) {
  if (config.node_id.empty() || config.spine_joint_id.empty() ||
      config.head_joint_id.empty() || config.torso_part_id.empty() ||
      config.ear_l_part_id.empty() || config.ear_r_part_id.empty() ||
      config.cloth_part_id.empty()) {
    return std::unexpected(make_err(
        "missing_field", "Secondary motion config ids must be non-empty"));
  }
  const auto &p = config.params;
  if (!finite_positive(p.fps) || !finite_positive(p.blink_period_s) ||
      !finite_positive(p.blink_duration_s) ||
      !finite_positive(p.breath_period_s) ||
      !finite_positive(p.sway_period_s) || !finite_positive(p.ear_period_s) ||
      !finite_positive(p.cloth_period_s)) {
    return std::unexpected(
        make_err("invalid_timing",
                 "Secondary motion periods and fps must be finite > 0"));
  }
  if (p.blink_duration_s + 1e-12 >= p.blink_period_s) {
    return std::unexpected(
        make_err("invalid_timing",
                 "blink_duration_s must be strictly less than blink_period_s"));
  }
  if (!finite_nonneg(p.breath_scale_amp) || !finite_nonneg(p.sway_amp_rad) ||
      !finite_nonneg(p.ear_amp_px) || !finite_nonneg(p.cloth_amp_px)) {
    return std::unexpected(
        make_err("invalid_amplitude",
                 "Secondary motion amplitudes must be finite >= 0"));
  }
  return {};
}

std::expected<PuppetSecondaryMotionPose, diagnostics::Error>
evaluate_puppet_secondary_motion(const PuppetSecondaryMotionConfig &config,
                                 std::int64_t frame) {
  if (auto ok = validate_puppet_secondary_motion_config(config); !ok) {
    return std::unexpected(ok.error());
  }
  if (frame < 0) {
    return std::unexpected(
        make_err("invalid_frame", "Secondary motion frame must be >= 0"));
  }

  const auto &p = config.params;
  const double t_s = static_cast<double>(frame) / p.fps;
  const auto frame_u = static_cast<std::uint64_t>(frame);

  // Blink: one pulse per period, start offset hashed per cycle (random-access).
  const double cycle_f = t_s / p.blink_period_s;
  const auto cycle = static_cast<std::uint64_t>(std::floor(cycle_f));
  const double cycle_local =
      (cycle_f - static_cast<double>(cycle)) * p.blink_period_s;
  const double start_frac = core::sample_unit_noise(config.seed, config.node_id,
                                                    "blink_phase", cycle);
  const double max_start = p.blink_period_s - p.blink_duration_s;
  const double blink_start = start_frac * max_start;
  const double blink =
      blink_envelope(cycle_local - blink_start, p.blink_duration_s);

  // Continuous analytic channels (no prior-frame state).
  const double breath_phase =
      t_s / p.breath_period_s +
      core::sample_unit_noise(config.seed, config.node_id, "breath_phase", 0);
  const double breath = 0.5 + 0.5 * sin01(breath_phase);

  const double sway_phase =
      t_s / p.sway_period_s +
      core::sample_unit_noise(config.seed, config.node_id, "sway_phase", 0);
  const double sway = sin01(sway_phase);

  const double ear_phase =
      t_s / p.ear_period_s +
      core::sample_unit_noise(config.seed, config.node_id, "ear_phase", 0);
  const double ear = sin01(ear_phase);

  const double cloth_phase =
      t_s / p.cloth_period_s +
      core::sample_unit_noise(config.seed, config.node_id, "cloth_phase", 0);
  const double cloth = sin01(cloth_phase);

  // Tiny high-frequency jitter from absolute frame hash (independent channels).
  const double sway_jitter =
      (core::sample_unit_noise(config.seed, config.node_id, "sway_jitter",
                               frame_u) -
       0.5) *
      0.15;
  const double cloth_jitter =
      (core::sample_unit_noise(config.seed, config.node_id, "cloth_jitter",
                               frame_u) -
       0.5) *
      0.25;

  PuppetSecondaryMotionPose pose;
  pose.channels.blink = clamp01(blink);

  pose.joint_poses.emplace(
      config.spine_joint_id,
      PuppetJointPose{.rotation_z_rad = p.sway_amp_rad * (sway + sway_jitter)});
  // Subtle head counter-sway keeps the face readable.
  pose.joint_poses.emplace(
      config.head_joint_id,
      PuppetJointPose{.rotation_z_rad =
                          -0.35 * p.sway_amp_rad * (sway + sway_jitter)});

  pose.parts.push_back(PuppetPartDriver{
      .part_id = config.torso_part_id,
      .scale_x = 1.0 + 0.35 * p.breath_scale_amp * breath,
      .scale_y = 1.0 + p.breath_scale_amp * breath,
      .opacity = 1.0,
      .translate_delta =
          core::Vec3{0, -1.5 * p.breath_scale_amp * breath * 40.0, 0},
  });
  pose.parts.push_back(PuppetPartDriver{
      .part_id = config.ear_l_part_id,
      .scale_x = 1.0,
      .scale_y = 1.0,
      .opacity = 1.0,
      .translate_delta =
          core::Vec3{-p.ear_amp_px * ear, 0.35 * p.ear_amp_px * ear, 0},
  });
  pose.parts.push_back(PuppetPartDriver{
      .part_id = config.ear_r_part_id,
      .scale_x = 1.0,
      .scale_y = 1.0,
      .opacity = 1.0,
      .translate_delta =
          core::Vec3{p.ear_amp_px * ear, 0.35 * p.ear_amp_px * ear, 0},
  });
  pose.parts.push_back(PuppetPartDriver{
      .part_id = config.cloth_part_id,
      .scale_x = 1.0,
      .scale_y = 1.0,
      .opacity = 1.0,
      .translate_delta = core::Vec3{p.cloth_amp_px * (cloth + cloth_jitter),
                                    0.25 * p.cloth_amp_px * cloth, 0},
  });

  return pose;
}

std::expected<PuppetExpressionPose, diagnostics::Error>
compose_puppet_expression_with_secondary(
    const PuppetExpressionPose &expression,
    const PuppetSecondaryMotionPose &secondary) {
  PuppetExpressionPose out = expression;

  // Joints: accumulate rotations/translations.
  for (const auto &[joint_id, pose] : secondary.joint_poses) {
    auto &dst = out.joint_poses[joint_id];
    dst.rotation_z_rad += pose.rotation_z_rad;
    dst.translate_delta.x += pose.translate_delta.x;
    dst.translate_delta.y += pose.translate_delta.y;
    dst.translate_delta.z += pose.translate_delta.z;
  }

  // Blink: keep the more closed lid (max blink weight → re-derive eye drivers
  // when eye parts are present; otherwise leave expression drivers alone).
  const double blink = std::max(0.0, std::max(0.0, secondary.channels.blink));
  // We do not have the original expression weights; apply blink as a scale
  // multiply on eye parts when present.
  for (auto &part : out.parts) {
    if (part.part_id == kPuppetPartEyeL || part.part_id == kPuppetPartEyeR) {
      const double open = std::max(0.0, 1.0 - 0.92 * blink);
      part.scale_y *= open;
      part.opacity = std::min(part.opacity, std::max(0.15, open));
    }
  }

  // Append / merge secondary-only parts by id.
  for (const auto &src : secondary.parts) {
    auto it = std::find_if(
        out.parts.begin(), out.parts.end(),
        [&](const PuppetPartDriver &p) { return p.part_id == src.part_id; });
    if (it == out.parts.end()) {
      out.parts.push_back(src);
      continue;
    }
    it->scale_x *= src.scale_x;
    it->scale_y *= src.scale_y;
    it->opacity = std::min(it->opacity, src.opacity);
    it->translate_delta.x += src.translate_delta.x;
    it->translate_delta.y += src.translate_delta.y;
    it->translate_delta.z += src.translate_delta.z;
  }

  return out;
}

std::string format_puppet_secondary_fixture(
    std::string_view label, const PuppetSecondaryMotionConfig &config,
    const PuppetSecondaryMotionPose &pose, std::int64_t frame) {
  std::ostringstream os;
  os << "puppet_secondary " << label << '\n';
  os << "node=" << config.node_id << " seed=" << config.seed
     << " frame=" << frame << std::fixed << std::setprecision(6)
     << " blink=" << pose.channels.blink << '\n';
  // Stable joint order by id.
  std::vector<std::string> joint_ids;
  joint_ids.reserve(pose.joint_poses.size());
  for (const auto &[id, _] : pose.joint_poses) {
    joint_ids.push_back(id);
  }
  std::sort(joint_ids.begin(), joint_ids.end());
  for (const auto &id : joint_ids) {
    const auto &jp = pose.joint_poses.at(id);
    os << "j " << id << " rz=" << jp.rotation_z_rad
       << " tx=" << jp.translate_delta.x << " ty=" << jp.translate_delta.y
       << '\n';
  }
  for (const auto &part : pose.parts) {
    os << "p " << part.part_id << " sx=" << part.scale_x
       << " sy=" << part.scale_y << " o=" << part.opacity
       << " tx=" << part.translate_delta.x << " ty=" << part.translate_delta.y
       << '\n';
  }
  return os.str();
}

} // namespace instantee::scene_ir
