#include <instantee/core_model/depth_stage.hpp>
#include <instantee/core_model/properties.hpp>
#include <instantee/direction/cinematic_camera.hpp>
#include <instantee/direction/motion_grammar.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/shot_planner.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace instantee::core;
using namespace instantee::direction;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] ShotPlanRequest make_shot_request(std::string_view id,
                                                std::uint64_t duration,
                                                IntentKind kind) {
  const std::array<std::string_view, 1> one{"subject"};
  const std::array<std::string_view, 1> known{"subject"};
  auto intent = make_semantic_intent(kind, one, 0.7, 0.4, std::nullopt, known);
  if (!intent) {
    std::cerr << "FAIL: semantic intent " << intent.error().code << '\n';
    std::exit(EXIT_FAILURE);
  }
  ShotPlanRequest request;
  request.shot_id = std::string(id);
  request.author_duration_frames = duration;
  request.intent = *intent;
  return request;
}

[[nodiscard]] bool
tracks_use_registered_properties(const std::vector<DirectedTrack> &tracks,
                                 const PropertyRegistry &properties) {
  for (const auto &track : tracks) {
    if (!properties.contains(track.property)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] DepthStage make_demo_stage() {
  // Keep the subject on the optical axis so framing demos measure move
  // stability rather than off-axis perspective blow-up.
  DepthStageSpacing spacing{};
  spacing.near_depth = 8.0;
  spacing.depth_step = 1.0;
  spacing.plane_width = 200;
  spacing.plane_height = 200;
  spacing.origin = {0, 0};
  spacing.focus_depth = 12.0;
  auto stage = make_layered_depth_stage(12, spacing);
  if (!stage) {
    std::cerr << "FAIL: demo stage " << stage.error().code << '\n';
    std::exit(EXIT_FAILURE);
  }
  return *stage;
}

} // namespace

int main() {
  auto properties = make_builtin_property_registry();
  auto shot = plan_shot(make_shot_request("cine_demo", 48, IntentKind::focus));
  expect(shot.has_value(), "plan shot");
  if (!shot) {
    return EXIT_FAILURE;
  }

  MotionGrammarBinding grammar{};
  grammar.package_id = "paper-cinema";
  grammar.intent = IntentKind::focus;
  grammar.pattern = GesturePattern::camera_move;
  grammar.ease = MotionEase::ease_in_out;
  grammar.duration_scale = 0.85;
  grammar.intensity_scale = 1.0;

  const std::array presets = {
      CinematicCameraPreset::dolly, CinematicCameraPreset::truck,
      CinematicCameraPreset::slow_zoom, CinematicCameraPreset::crane_pan,
      CinematicCameraPreset::rack_depth};

  {
    expect(parse_cinematic_camera_preset("crane_pan").value() ==
               CinematicCameraPreset::crane_pan,
           "parse crane_pan");
    expect(!parse_cinematic_camera_preset("orbit").has_value(),
           "reject unknown preset");
  }

  {
    // Fail closed when a required capability is missing.
    CinematicCameraRequest req;
    req.shot = *shot;
    req.camera_node_id = "camera";
    req.preset = CinematicCameraPreset::truck;
    req.allowed = CameraCapabilitySet::none();
    req.allowed.allow(CameraCapability::dolly);
    expect(!plan_cinematic_camera_preset(req).has_value(),
           "truck without pan rejected");
  }

  {
    CinematicCameraRequest bad_grammar;
    bad_grammar.shot = *shot;
    bad_grammar.camera_node_id = "camera";
    bad_grammar.preset = CinematicCameraPreset::dolly;
    MotionGrammarBinding settle = grammar;
    settle.pattern = GesturePattern::settle;
    bad_grammar.grammar = settle;
    expect(!plan_cinematic_camera_preset(bad_grammar).has_value(),
           "non-camera_move grammar rejected");
  }

  PerspectiveFrustum frustum;
  frustum.fov_y_rad = std::numbers::pi_v<double> / 3.0;
  frustum.aspect = 1080.0 / 1920.0;
  frustum.near_distance = 1.0;
  frustum.far_distance = 100.0;
  Viewport viewport{0, 0, 1080, 1920};
  const DepthStage stage = make_demo_stage();

  for (CinematicCameraPreset preset : presets) {
    CinematicCameraRequest req;
    req.shot = *shot;
    req.camera_node_id = "camera";
    req.preset = preset;
    req.intensity = 0.35;
    req.grammar = grammar;
    req.allowed = CameraCapabilitySet::all_v1();

    auto tracks = plan_cinematic_camera_preset(req);
    expect(tracks.has_value(), "plan preset");
    if (!tracks) {
      continue;
    }
    expect(!tracks->empty(), "preset emits tracks");
    expect(tracks_use_registered_properties(*tracks, properties),
           "registered properties only");

    // Deterministic: identical plans.
    auto again = plan_cinematic_camera_preset(req);
    expect(again.has_value() && again->size() == tracks->size(), "replan");
    if (again) {
      for (std::size_t i = 0; i < tracks->size(); ++i) {
        expect((*tracks)[i].property == (*again)[i].property,
               "stable property");
        expect((*tracks)[i].keyframes.size() == (*again)[i].keyframes.size(),
               "stable key count");
      }
    }

    // Two keyframes only → smooth eased motion (no authored jitter).
    for (const auto &track : *tracks) {
      expect(track.keyframes.size() <= 2, "at most two keys per property");
      for (const auto &kf : track.keyframes) {
        expect(kf.frame < shot->duration_frames, "keys inside budget");
        expect(std::isfinite(kf.value), "finite key value");
      }
    }

    auto samples = sample_cinematic_preset_on_stage(req, stage, /*subject*/ 2,
                                                    frustum, viewport);
    expect(samples.has_value(), "demo samples");
    if (!samples) {
      continue;
    }
    expect(samples->size() == shot->duration_frames, "one sample per frame");

    // Same scene under each preset: framing stays in-viewport and motion has
    // no distracting direction-reversal jitter.
    const auto stability =
        measure_framing_stability(*samples, /*min_step_px=*/0.05,
                                  /*max_jerk_px=*/25.0);
    expect(stability.direction_reversals == 0, "no direction reversals");
    expect(stability.max_jerk_px <= 25.0, "bounded jerk");
    expect(stability.ok, "stable framing without jitter");

    for (const CinematicSample &s : *samples) {
      expect(std::isfinite(s.anchor_pixel.x) && std::isfinite(s.anchor_pixel.y),
             "finite projection");
      expect(s.anchor_pixel.x >= 0.0 && s.anchor_pixel.x <= viewport.width &&
                 s.anchor_pixel.y >= 0.0 && s.anchor_pixel.y <= viewport.height,
             "subject remains framed");
    }
  }

  {
    // Rack-depth uses focus (required) and may include optional dolly.
    const auto required =
        required_capabilities_for_preset(CinematicCameraPreset::rack_depth);
    expect(!required.empty() && required.front() == CameraCapability::focus,
           "rack requires focus");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
