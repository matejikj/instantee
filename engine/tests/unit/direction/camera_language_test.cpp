#include <instantee/core_model/properties.hpp>
#include <instantee/direction/camera_language.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/shot_planner.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::core::AnimatableTargetKind;
using instantee::core::make_builtin_property_registry;
using instantee::direction::CameraCapability;
using instantee::direction::CameraCapabilitySet;
using instantee::direction::CameraFraming;
using instantee::direction::CameraLanguageRequest;
using instantee::direction::IntentKind;
using instantee::direction::make_semantic_intent;
using instantee::direction::plan_camera_language;
using instantee::direction::plan_shot;
using instantee::direction::required_capabilities_for;
using instantee::direction::ShotPlanRequest;

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
  const std::array<std::string_view, 2> known{"a", "b"};
  const std::array<std::string_view, 1> one{"a"};
  const std::array<std::string_view, 2> two{"a", "b"};
  const auto targets = (kind == IntentKind::compare)
                           ? std::span<const std::string_view>(two)
                           : std::span<const std::string_view>(one);
  auto intent =
      make_semantic_intent(kind, targets, 0.8, 0.5, std::nullopt, known);
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
track_in_budget(const instantee::direction::DirectedTrack &track,
                std::uint64_t budget) {
  for (const auto &kf : track.keyframes) {
    if (kf.frame >= budget) {
      return false;
    }
  }
  return !track.keyframes.empty();
}

[[nodiscard]] bool has_capability_track(
    const std::vector<instantee::direction::DirectedTrack> &tracks,
    CameraCapability capability) {
  const auto needle =
      std::string("_cam_") +
      std::string(instantee::direction::camera_capability_id(capability)) + "_";
  for (const auto &track : tracks) {
    if (track.id.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  auto properties = make_builtin_property_registry();

  // Declared V1 capabilities cover the framing vocabulary.
  {
    auto required_push = required_capabilities_for(CameraFraming::push_in);
    expect(required_push.size() == 1 &&
               required_push.front() == CameraCapability::dolly,
           "push_in requires dolly");
    auto required_orbit = required_capabilities_for(CameraFraming::orbit);
    expect(required_orbit.size() == 2, "orbit requires two moves");
    expect(required_capabilities_for(CameraFraming::static_framing).empty(),
           "static requires nothing");
  }

  // Full allow-list: push_in emits dolly + optional zoom on registered props.
  {
    auto shot = plan_shot(make_shot_request("s_push", 60, IntentKind::reveal));
    expect(shot.has_value(), "plan push shot");
    if (shot) {
      expect(shot->framing == CameraFraming::push_in, "reveal → push_in");
      CameraLanguageRequest req;
      req.shot = *shot;
      req.camera_node_id = "main_cam";
      req.allowed = CameraCapabilitySet::all_v1();
      req.intensity = 0.75;
      auto tracks = plan_camera_language(req);
      expect(tracks.has_value(), "camera language push_in");
      if (tracks) {
        expect(has_capability_track(*tracks, CameraCapability::dolly),
               "dolly track present");
        expect(has_capability_track(*tracks, CameraCapability::zoom),
               "zoom companion present");
        for (const auto &track : *tracks) {
          expect(track.target_id == "main_cam", "targets camera node");
          expect(track_in_budget(track, shot->duration_frames),
                 "keyframes inside shot");
          auto prop = properties.resolve_for_target(AnimatableTargetKind::node,
                                                    track.property);
          expect(prop.has_value(), "property is registered for nodes");
        }
      }
    }
  }

  // Director without zoom: required dolly still works; zoom omitted.
  {
    auto shot =
        plan_shot(make_shot_request("s_nozoom", 48, IntentKind::reveal));
    expect(shot.has_value(), "plan nozoom shot");
    if (shot) {
      CameraCapabilitySet allowed;
      allowed.allow(CameraCapability::dolly);
      CameraLanguageRequest req;
      req.shot = *shot;
      req.camera_node_id = "main_cam";
      req.allowed = allowed;
      req.intensity = 0.5;
      auto tracks = plan_camera_language(req);
      expect(tracks.has_value(), "dolly-only director accepted");
      if (tracks) {
        expect(has_capability_track(*tracks, CameraCapability::dolly),
               "dolly kept");
        expect(!has_capability_track(*tracks, CameraCapability::zoom),
               "zoom not invented");
      }
    }
  }

  // Director missing a required capability fails closed.
  {
    auto shot = plan_shot(make_shot_request("s_pan", 40, IntentKind::compare));
    expect(shot.has_value() && shot->framing == CameraFraming::pan,
           "compare → pan");
    if (shot) {
      CameraCapabilitySet allowed;
      allowed.allow(CameraCapability::dolly); // pan missing
      CameraLanguageRequest req;
      req.shot = *shot;
      req.camera_node_id = "main_cam";
      req.allowed = allowed;
      auto tracks = plan_camera_language(req);
      expect(!tracks.has_value(), "undeclared pan rejected");
      if (!tracks) {
        expect(tracks.error().code == "undeclared_camera_capability",
               "capability error code");
      }
    }
  }

  // Orbit requires pan+dolly; focus is optional.
  {
    auto shot =
        plan_shot(make_shot_request("s_orbit", 90, IntentKind::build_suspense));
    expect(shot.has_value() && shot->framing == CameraFraming::orbit,
           "suspense → orbit");
    if (shot) {
      CameraCapabilitySet minimal;
      minimal.allow(CameraCapability::pan);
      minimal.allow(CameraCapability::dolly);
      CameraLanguageRequest req;
      req.shot = *shot;
      req.camera_node_id = "cam";
      req.allowed = minimal;
      req.intensity = 1.0;
      auto tracks = plan_camera_language(req);
      expect(tracks.has_value(), "orbit with required caps");
      if (tracks) {
        expect(has_capability_track(*tracks, CameraCapability::pan), "pan");
        expect(has_capability_track(*tracks, CameraCapability::dolly), "dolly");
        expect(!has_capability_track(*tracks, CameraCapability::focus),
               "optional focus skipped");
      }

      CameraCapabilitySet missing_dolly;
      missing_dolly.allow(CameraCapability::pan);
      req.allowed = missing_dolly;
      expect(!plan_camera_language(req).has_value(),
             "orbit without dolly rejected");
    }
  }

  // Static framing emits no camera tracks.
  {
    auto shot =
        plan_shot(make_shot_request("s_static", 30, IntentKind::explain));
    expect(shot.has_value() && shot->framing == CameraFraming::static_framing,
           "explain → static");
    if (shot) {
      CameraLanguageRequest req;
      req.shot = *shot;
      req.camera_node_id = "main_cam";
      auto tracks = plan_camera_language(req);
      expect(tracks.has_value() && tracks->empty(), "static → no tracks");
    }
  }

  // Determinism.
  {
    auto shot = plan_shot(make_shot_request("s_det", 72, IntentKind::focus));
    expect(shot.has_value(), "det shot");
    if (shot) {
      CameraLanguageRequest req;
      req.shot = *shot;
      req.camera_node_id = "main_cam";
      req.intensity = 0.6;
      auto a = plan_camera_language(req);
      auto b = plan_camera_language(req);
      expect(a.has_value() && b.has_value() && a->size() == b->size(),
             "deterministic size");
      if (a && b && a->size() == b->size()) {
        for (std::size_t i = 0; i < a->size(); ++i) {
          expect((*a)[i].id == (*b)[i].id &&
                     (*a)[i].property == (*b)[i].property &&
                     (*a)[i].keyframes.size() == (*b)[i].keyframes.size(),
                 "deterministic tracks");
        }
      }
    }
  }

  // Empty camera node / empty allow-list on required move.
  {
    auto shot = plan_shot(make_shot_request("s_err", 24, IntentKind::reveal));
    expect(shot.has_value(), "err shot");
    if (shot) {
      CameraLanguageRequest req;
      req.shot = *shot;
      req.camera_node_id = "";
      expect(!plan_camera_language(req).has_value(), "empty camera rejected");
      req.camera_node_id = "main_cam";
      req.allowed = CameraCapabilitySet::none();
      expect(!plan_camera_language(req).has_value(),
             "empty capability set rejected for push_in");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
