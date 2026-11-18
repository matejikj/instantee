#include <instantee/scene_ir/puppet_walk.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "puppet_walk", message};
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

// Smoothstep ease for start/stop envelopes.
[[nodiscard]] double smoothstep01(double t) noexcept {
  const double x = clamp01(t);
  return x * x * (3.0 - 2.0 * x);
}

[[nodiscard]] PuppetJointPose pose_rz(double rad) noexcept {
  return PuppetJointPose{.rotation_z_rad = rad};
}

[[nodiscard]] PuppetJointPose pose_rz_ty(double rad, double ty) noexcept {
  return PuppetJointPose{.rotation_z_rad = rad,
                         .translate_delta = core::Vec3{0, ty, 0}};
}

struct GaitAngles {
  double hip_l = 0;
  double knee_l = 0;
  double foot_l = 0;
  double hip_r = 0;
  double knee_r = 0;
  double foot_r = 0;
  double lift_l = 0;
  double lift_r = 0;
};

// Side-view gait keyed by cycle_t in [0, 1). Left plants near 0, right near
// 0.5.
[[nodiscard]] GaitAngles gait_at(double cycle_t, double step_height) noexcept {
  const double t = cycle_t - std::floor(cycle_t);
  const double two_pi = 6.283185307179586;
  // Opposing hip swings; knees flex on swing peaks (t≈0.25 / 0.75).
  const double hip_amp = 0.45;
  const double knee_flex = 0.85;
  GaitAngles g;
  g.hip_l = hip_amp * std::sin(two_pi * t);
  g.hip_r = hip_amp * std::sin(two_pi * t + 3.141592653589793);
  const double swing_l = 0.5 * (1.0 - std::cos(two_pi * (t - 0.0)));
  const double swing_r = 0.5 * (1.0 - std::cos(two_pi * (t - 0.5)));
  // Knee flex mainly during swing half-cycles.
  const bool left_swing = t >= 0.45 && t < 0.95;
  const bool right_swing = t < 0.45 || t >= 0.95;
  g.knee_l = left_swing ? knee_flex * swing_l : 0.08 * std::sin(two_pi * t);
  g.knee_r = right_swing ? knee_flex * swing_r
                         : 0.08 * std::sin(two_pi * t + 3.141592653589793);
  g.foot_l = -0.35 * g.hip_l;
  g.foot_r = -0.35 * g.hip_r;
  g.lift_l = left_swing ? -step_height * swing_l : 0.0;
  g.lift_r = right_swing ? -step_height * swing_r : 0.0;
  return g;
}

[[nodiscard]] bool left_planted(double cycle_t) noexcept {
  const double t = cycle_t - std::floor(cycle_t);
  return t < 0.45;
}

[[nodiscard]] bool right_planted(double cycle_t) noexcept {
  const double t = cycle_t - std::floor(cycle_t);
  return t >= 0.5 && t < 0.95;
}

struct PhaseBreakdown {
  PuppetLocomotionPhase phase = PuppetLocomotionPhase::idle;
  double root_travel_x = 0;
  double cycle_units = 0;
  double cycle_t = 0;
  double gait_blend = 0; // 0 = idle stance, 1 = full gait angles
};

[[nodiscard]] std::expected<PhaseBreakdown, diagnostics::Error>
breakdown_frame(const PuppetWalkPlan &plan, double frame) {
  if (auto ok = validate_puppet_walk_plan(plan); !ok) {
    return std::unexpected(ok.error());
  }
  const auto total = puppet_walk_total_frames(plan);
  if (!total) {
    return std::unexpected(total.error());
  }
  const double f = std::isfinite(frame) ? std::max(0.0, frame) : 0.0;
  const auto &tm = plan.timing;
  const double cruise_dist = plan.travel_distance_px;
  const double cycles = cruise_dist / tm.stride_px;
  const double cruise_frames = cycles * tm.cycle_frames;
  const double start_end = tm.start_frames;
  const double cruise_end = start_end + cruise_frames;
  const double stop_end = cruise_end + tm.stop_frames;

  PhaseBreakdown out;
  if (f <= 0.0 || (tm.start_frames <= 0.0 && cruise_frames <= 0.0)) {
    out.phase = PuppetLocomotionPhase::idle;
    return out;
  }

  if (f < start_end) {
    out.phase = PuppetLocomotionPhase::start;
    const double u = smoothstep01(f / tm.start_frames);
    // Start covers the first half-stride of travel while blending into gait.
    out.root_travel_x = 0.5 * tm.stride_px * u;
    out.cycle_units = out.root_travel_x / tm.stride_px;
    out.cycle_t = out.cycle_units - std::floor(out.cycle_units);
    out.gait_blend = u;
    return out;
  }

  if (f < cruise_end) {
    out.phase = PuppetLocomotionPhase::cycle;
    const double along = f - start_end;
    // After start's half-stride, cruise covers the remainder of
    // travel_distance.
    const double start_travel = 0.5 * tm.stride_px;
    const double cruise_travel = cruise_dist - start_travel;
    const double u = cruise_frames > 0.0 ? along / cruise_frames : 1.0;
    out.root_travel_x = start_travel + cruise_travel * u;
    out.cycle_units = out.root_travel_x / tm.stride_px;
    out.cycle_t = out.cycle_units - std::floor(out.cycle_units);
    out.gait_blend = 1.0;
    return out;
  }

  if (f < stop_end) {
    out.phase = PuppetLocomotionPhase::stop;
    const double along = f - cruise_end;
    const double u = smoothstep01(along / tm.stop_frames);
    out.root_travel_x = cruise_dist;
    out.cycle_units = cruise_dist / tm.stride_px;
    // Settle toward a neutral plant during stop.
    out.cycle_t = 0.0;
    out.gait_blend = 1.0 - u;
    return out;
  }

  out.phase = PuppetLocomotionPhase::idle;
  out.root_travel_x = cruise_dist;
  out.cycle_units = cruise_dist / tm.stride_px;
  out.cycle_t = 0.0;
  out.gait_blend = 0.0;
  return out;
}

[[nodiscard]] std::expected<std::string, diagnostics::Error>
require_role(const PuppetWalkRetargetMap &retarget, std::string_view role) {
  const auto it = retarget.find(std::string(role));
  if (it == retarget.end() || it->second.empty()) {
    return std::unexpected(make_err("missing_retarget",
                                    "Walk role has no retarget joint mapping"));
  }
  return it->second;
}

} // namespace

PuppetRigDocument make_biped_walk_rig() {
  PuppetRigDocument doc;
  doc.schema_version = std::string{kPuppetRigSchemaVersion};
  doc.id = "biped-walk";
  doc.retarget.profile = std::string{kPuppetRigRetargetProfile};
  doc.retarget.space = std::string{kPuppetRigRetargetSpace};
  // Side-view biped; +Y down. Slight X offsets keep L/R layers distinct.
  doc.joints = {
      PuppetJoint{.id = "root", .pivot = {0, 0}},
      PuppetJoint{.id = "hip_l", .parent_id = "root", .pivot = {-6, 18}},
      PuppetJoint{.id = "knee_l", .parent_id = "hip_l", .pivot = {-6, 44}},
      PuppetJoint{.id = "foot_l", .parent_id = "knee_l", .pivot = {-6, 72}},
      PuppetJoint{.id = "hip_r", .parent_id = "root", .pivot = {6, 18}},
      PuppetJoint{.id = "knee_r", .parent_id = "hip_r", .pivot = {6, 44}},
      PuppetJoint{.id = "foot_r", .parent_id = "knee_r", .pivot = {6, 72}},
  };
  doc.layers = {
      PuppetLayer{.id = "leg_l", .bind_joint_id = "knee_l"},
      PuppetLayer{.id = "leg_r", .bind_joint_id = "knee_r"},
  };
  doc.skin_bindings = {
      PuppetSkinBinding{.layer_id = "leg_l",
                        .influences = {PuppetSkinInfluence{"knee_l", 1.0}}},
      PuppetSkinBinding{.layer_id = "leg_r",
                        .influences = {PuppetSkinInfluence{"knee_r", 1.0}}},
  };
  doc.constraints = {
      PuppetConstraint{.id = "knee_l_hinge",
                       .type = PuppetConstraintType::hinge_limit,
                       .joint_id = "knee_l",
                       .min_rad = 0.0,
                       .max_rad = 2.2},
      PuppetConstraint{.id = "knee_r_hinge",
                       .type = PuppetConstraintType::hinge_limit,
                       .joint_id = "knee_r",
                       .min_rad = 0.0,
                       .max_rad = 2.2},
  };
  return doc;
}

PuppetWalkRetargetMap make_biped_walk_retarget_map() noexcept {
  return {
      {std::string(kPuppetWalkRoleRoot), "root"},
      {std::string(kPuppetWalkRoleHipL), "hip_l"},
      {std::string(kPuppetWalkRoleKneeL), "knee_l"},
      {std::string(kPuppetWalkRoleFootL), "foot_l"},
      {std::string(kPuppetWalkRoleHipR), "hip_r"},
      {std::string(kPuppetWalkRoleKneeR), "knee_r"},
      {std::string(kPuppetWalkRoleFootR), "foot_r"},
  };
}

std::expected<void, diagnostics::Error>
validate_puppet_walk_plan(const PuppetWalkPlan &plan) {
  const auto &tm = plan.timing;
  if (!(plan.travel_distance_px > 0.0) ||
      !std::isfinite(plan.travel_distance_px)) {
    return std::unexpected(make_err(
        "invalid_travel", "travel_distance_px must be finite and > 0"));
  }
  if (!(tm.cycle_frames > 0.0) || !std::isfinite(tm.cycle_frames) ||
      !(tm.start_frames >= 0.0) || !std::isfinite(tm.start_frames) ||
      !(tm.stop_frames >= 0.0) || !std::isfinite(tm.stop_frames) ||
      !(tm.stride_px > 0.0) || !std::isfinite(tm.stride_px) ||
      !(tm.step_height_px >= 0.0) || !std::isfinite(tm.step_height_px) ||
      !(tm.foot_slide_tol_px >= 0.0) || !std::isfinite(tm.foot_slide_tol_px)) {
    return std::unexpected(make_err(
        "invalid_timing", "Walk timing fields must be finite and valid"));
  }
  if (plan.travel_distance_px + 1e-9 < 0.5 * tm.stride_px) {
    return std::unexpected(make_err(
        "invalid_travel",
        "travel_distance_px must cover at least the start half-stride"));
  }
  return {};
}

std::expected<double, diagnostics::Error>
puppet_walk_total_frames(const PuppetWalkPlan &plan) {
  if (auto ok = validate_puppet_walk_plan(plan); !ok) {
    return std::unexpected(ok.error());
  }
  const auto &tm = plan.timing;
  const double cycles = plan.travel_distance_px / tm.stride_px;
  return tm.start_frames + cycles * tm.cycle_frames + tm.stop_frames;
}

std::expected<PuppetWalkSample, diagnostics::Error>
sample_puppet_walk(const PuppetWalkPlan &plan,
                   const PuppetWalkRetargetMap &retarget, double frame) {
  auto phase = breakdown_frame(plan, frame);
  if (!phase) {
    return std::unexpected(phase.error());
  }

  const auto root_id = require_role(retarget, kPuppetWalkRoleRoot);
  if (!root_id) {
    return std::unexpected(root_id.error());
  }
  const auto hip_l = require_role(retarget, kPuppetWalkRoleHipL);
  if (!hip_l) {
    return std::unexpected(hip_l.error());
  }
  const auto knee_l = require_role(retarget, kPuppetWalkRoleKneeL);
  if (!knee_l) {
    return std::unexpected(knee_l.error());
  }
  const auto foot_l = require_role(retarget, kPuppetWalkRoleFootL);
  if (!foot_l) {
    return std::unexpected(foot_l.error());
  }
  const auto hip_r = require_role(retarget, kPuppetWalkRoleHipR);
  if (!hip_r) {
    return std::unexpected(hip_r.error());
  }
  const auto knee_r = require_role(retarget, kPuppetWalkRoleKneeR);
  if (!knee_r) {
    return std::unexpected(knee_r.error());
  }
  const auto foot_r = require_role(retarget, kPuppetWalkRoleFootR);
  if (!foot_r) {
    return std::unexpected(foot_r.error());
  }

  const GaitAngles gait = gait_at(phase->cycle_t, plan.timing.step_height_px);
  const double b = phase->gait_blend;

  PuppetWalkSample sample;
  sample.phase = phase->phase;
  sample.frame = std::isfinite(frame) ? std::max(0.0, frame) : 0.0;
  sample.cycle_units = phase->cycle_units;
  sample.cycle_t = phase->cycle_t;
  sample.root_travel_x = phase->root_travel_x;

  // Relative gait (root translation applied later via plant lock).
  sample.joint_poses.emplace(*hip_l, pose_rz(gait.hip_l * b));
  sample.joint_poses.emplace(*knee_l, pose_rz(gait.knee_l * b));
  sample.joint_poses.emplace(*foot_l,
                             pose_rz_ty(gait.foot_l * b, gait.lift_l * b));
  sample.joint_poses.emplace(*hip_r, pose_rz(gait.hip_r * b));
  sample.joint_poses.emplace(*knee_r, pose_rz(gait.knee_r * b));
  sample.joint_poses.emplace(*foot_r,
                             pose_rz_ty(gait.foot_r * b, gait.lift_r * b));

  // Analytic plant anchors from cycle units (random-access; no prior frame).
  const double units = phase->cycle_units;
  const double completed = std::floor(units);
  const bool lock_l = left_planted(phase->cycle_t) && b > 1e-6;
  const bool lock_r = right_planted(phase->cycle_t) && b > 1e-6;
  if (lock_l) {
    sample.planted.foot_id = *foot_l;
    sample.planted.plant_world_x = plan.timing.stride_px * completed;
    sample.planted.locked = true;
  } else if (lock_r) {
    sample.planted.foot_id = *foot_r;
    sample.planted.plant_world_x = plan.timing.stride_px * (completed + 0.5);
    sample.planted.locked = true;
  }

  // Tentative root travel; plant correction is applied in evaluate.
  sample.joint_poses.emplace(
      *root_id, PuppetJointPose{.translate_delta =
                                    core::Vec3{phase->root_travel_x, 0, 0}});

  return sample;
}

std::expected<PuppetFkSkeleton, diagnostics::Error>
evaluate_puppet_walk(const PuppetRigDocument &document,
                     const PuppetFkSkeleton &bind_skeleton,
                     const PuppetWalkSample &sample) {
  PuppetFkSkeleton posed = bind_skeleton;
  if (auto ok = evaluate_puppet_fk(posed, document, sample.joint_poses); !ok) {
    return std::unexpected(ok.error());
  }
  if (!sample.planted.locked) {
    return posed;
  }
  const auto it = posed.index_by_id.find(sample.planted.foot_id);
  if (it == posed.index_by_id.end()) {
    return std::unexpected(
        make_err("unknown_joint", "Planted foot id is not in the skeleton"));
  }
  const double foot_x = puppet_fk_world_position(posed.joints[it->second]).x;
  const double correction = sample.planted.plant_world_x - foot_x;

  auto adjusted = sample.joint_poses;
  // Root must exist in the pose map (sample_puppet_walk always writes it).
  bool found_root = false;
  for (auto &[joint_id, pose] : adjusted) {
    // Identify root as the joint whose bind parent is absent.
    const auto jit = posed.index_by_id.find(joint_id);
    if (jit != posed.index_by_id.end() &&
        !posed.joints[jit->second].parent_index.has_value()) {
      pose.translate_delta.x += correction;
      found_root = true;
      break;
    }
  }
  if (!found_root) {
    return std::unexpected(
        make_err("missing_root", "Walk sample has no root joint pose"));
  }

  posed = bind_skeleton;
  if (auto ok = evaluate_puppet_fk(posed, document, adjusted); !ok) {
    return std::unexpected(ok.error());
  }
  return posed;
}

std::expected<double, diagnostics::Error> measure_puppet_walk_foot_slide(
    const PuppetRigDocument &document, const PuppetFkSkeleton &bind_skeleton,
    const PuppetWalkPlan &plan, const PuppetWalkRetargetMap &retarget,
    double frame_begin, double frame_end) {
  if (!std::isfinite(frame_begin) || !std::isfinite(frame_end) ||
      frame_end < frame_begin) {
    return std::unexpected(
        make_err("invalid_range", "Foot-slide frame range is invalid"));
  }
  double max_slide = 0.0;
  std::optional<std::pair<std::string, double>> prev;
  const int begin_i = static_cast<int>(std::floor(frame_begin));
  const int end_i = static_cast<int>(std::floor(frame_end));
  for (int fi = begin_i; fi <= end_i; ++fi) {
    auto sample = sample_puppet_walk(plan, retarget, static_cast<double>(fi));
    if (!sample) {
      return std::unexpected(sample.error());
    }
    auto posed = evaluate_puppet_walk(document, bind_skeleton, *sample);
    if (!posed) {
      return std::unexpected(posed.error());
    }
    if (!sample->planted.locked) {
      prev.reset();
      continue;
    }
    const auto it = posed->index_by_id.find(sample->planted.foot_id);
    if (it == posed->index_by_id.end()) {
      return std::unexpected(
          make_err("unknown_joint", "Planted foot missing after evaluate"));
    }
    const double x = puppet_fk_world_position(posed->joints[it->second]).x;
    if (prev && prev->first == sample->planted.foot_id) {
      max_slide = std::max(max_slide, std::abs(x - prev->second));
    }
    prev = std::make_pair(sample->planted.foot_id, x);
  }
  return max_slide;
}

std::string format_puppet_walk_golden(std::string_view label,
                                      const PuppetWalkSample &sample,
                                      const PuppetFkSkeleton &posed,
                                      double foot_slide_px) {
  std::ostringstream os;
  os << "puppet_walk " << label << '\n';
  os << "phase=";
  switch (sample.phase) {
  case PuppetLocomotionPhase::idle:
    os << "idle";
    break;
  case PuppetLocomotionPhase::start:
    os << "start";
    break;
  case PuppetLocomotionPhase::cycle:
    os << "cycle";
    break;
  case PuppetLocomotionPhase::stop:
    os << "stop";
    break;
  }
  os << " frame=" << std::fixed << std::setprecision(4) << sample.frame
     << " cycle_t=" << sample.cycle_t << " root_x=" << sample.root_travel_x
     << " slide=" << foot_slide_px << '\n';
  if (sample.planted.locked) {
    os << "plant " << sample.planted.foot_id << " x=" << std::setprecision(6)
       << sample.planted.plant_world_x << '\n';
  } else {
    os << "plant none\n";
  }
  for (const auto &joint : posed.joints) {
    const auto p = puppet_fk_world_position(joint);
    os << "j " << joint.id << " x=" << std::setprecision(6) << p.x
       << " y=" << p.y << " rz=" << joint.local.rotation_z_rad << '\n';
  }
  return os.str();
}

} // namespace instantee::scene_ir
