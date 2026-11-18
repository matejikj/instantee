#include <instantee/direction/paper_cinematic_templates.hpp>

#include <array>
#include <span>
#include <sstream>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_cinematic_templates", message};
}

[[nodiscard]] bool intensity_ok(double v) noexcept {
  return v >= 0.0 && v <= 1.0;
}

} // namespace

std::expected<PaperCinematicShotKind, diagnostics::Error>
parse_paper_cinematic_shot_kind(std::string_view text) {
  if (text == "reveal_stage") {
    return PaperCinematicShotKind::reveal_stage;
  }
  if (text == "object_closeup") {
    return PaperCinematicShotKind::object_closeup;
  }
  if (text == "puppet_dialogue") {
    return PaperCinematicShotKind::puppet_dialogue;
  }
  if (text == "technical_explanation") {
    return PaperCinematicShotKind::technical_explanation;
  }
  if (text == "atmospheric_dissolve") {
    return PaperCinematicShotKind::atmospheric_dissolve;
  }
  return std::unexpected(
      make_err("unknown_shot_kind", "Unknown paper cinematic shot kind"));
}

std::expected<PaperCinematicSequenceAssembly, diagnostics::Error>
assemble_paper_cinematic_sequence(
    const PaperCinematicSequenceRequest &request) {
  if (request.film_id.empty() || request.camera_node_id.empty()) {
    return std::unexpected(
        make_err("missing_id", "film_id and camera_node_id are required"));
  }
  if (request.shots.empty()) {
    return std::unexpected(
        make_err("empty_sequence", "At least one shot template is required"));
  }

  static constexpr PaperCinematicShotKind kCanonical[] = {
      PaperCinematicShotKind::reveal_stage,
      PaperCinematicShotKind::object_closeup,
      PaperCinematicShotKind::puppet_dialogue,
      PaperCinematicShotKind::technical_explanation,
      PaperCinematicShotKind::atmospheric_dissolve,
  };

  // Full reel must keep the curated five-shot order; shorter prefixes are ok
  // for unit examples as long as kinds stay in canonical order without gaps.
  if (request.shots.size() > 5U) {
    return std::unexpected(
        make_err("invalid_shot_count",
                 "Paper cinematic templates max out at five shots"));
  }
  for (std::size_t i = 0; i < request.shots.size(); ++i) {
    if (request.shots[i].kind != kCanonical[i]) {
      return std::unexpected(make_err(
          "invalid_shot_order",
          "Shots must follow reveal_stage → object_closeup → "
          "puppet_dialogue → technical_explanation → atmospheric_dissolve"));
    }
    if (request.shots[i].duration_frames == 0) {
      return std::unexpected(make_err("invalid_duration",
                                      "Each shot duration_frames must be > 0"));
    }
    if (!intensity_ok(request.shots[i].camera_intensity)) {
      return std::unexpected(
          make_err("invalid_intensity", "camera_intensity must be in [0, 1]"));
    }
  }

  const std::array<std::string_view, 1> one{"subject"};
  const std::array<std::string_view, 2> pair{"subject_a", "subject_b"};
  const std::array<std::string_view, 3> known{"subject", "subject_a",
                                              "subject_b"};

  std::vector<ShotPlanRequest> plan_reqs;
  plan_reqs.reserve(request.shots.size());
  for (const auto &spec : request.shots) {
    const auto intent_kind = paper_cinematic_intent_for(spec.kind);
    const auto targets = intent_kind == IntentKind::compare
                             ? std::span<const std::string_view>{pair}
                             : std::span<const std::string_view>{one};
    auto intent = make_semantic_intent(intent_kind, targets, 0.7, 0.5,
                                       std::nullopt, known);
    if (!intent) {
      return std::unexpected(intent.error());
    }
    ShotPlanRequest pr;
    pr.shot_id = std::string(paper_cinematic_shot_kind_id(spec.kind));
    pr.author_duration_frames = spec.duration_frames;
    pr.intent = *intent;
    plan_reqs.push_back(std::move(pr));
  }

  auto film = plan_film_shots(plan_reqs);
  if (!film) {
    return std::unexpected(film.error());
  }

  PaperCinematicSequenceAssembly out;
  out.film_id = request.film_id;
  out.total_duration_frames = film->duration_frames;
  out.shots.reserve(request.shots.size());

  std::vector<ShotTiming> timings;
  timings.reserve(request.shots.size());
  std::vector<TransitionIntent> transition_intents;

  for (std::size_t i = 0; i < request.shots.size(); ++i) {
    const auto &spec = request.shots[i];
    const auto &plan = film->shots[i];

    CinematicCameraRequest cam;
    cam.shot = plan;
    cam.camera_node_id = request.camera_node_id;
    cam.preset = paper_cinematic_camera_preset_for(spec.kind);
    cam.intensity = spec.camera_intensity;
    auto tracks = plan_cinematic_camera_preset(cam);
    if (!tracks) {
      return std::unexpected(tracks.error());
    }

    PaperCinematicShotInstance inst;
    inst.kind = spec.kind;
    inst.shot_id = plan.shot_id;
    inst.start_frame = plan.start_frame;
    inst.duration_frames = plan.duration_frames;
    inst.intent = plan.intent;
    inst.framing = plan.framing;
    inst.camera_preset = cam.preset;
    inst.lighting_recipe_id =
        std::string(paper_cinematic_lighting_recipe_id_for(spec.kind));
    inst.plan = plan;
    inst.camera_tracks = std::move(*tracks);
    timings.push_back(ShotTiming{.id = inst.shot_id,
                                 .start_frame = inst.start_frame,
                                 .duration_frames = inst.duration_frames});

    // Atmospheric dissolve lowers to a reveal (Scene IR "dissolve") blend
    // from the previous shot when overlap is requested.
    if (spec.kind == PaperCinematicShotKind::atmospheric_dissolve && i > 0 &&
        spec.dissolve_overlap_frames > 0) {
      TransitionIntent tr;
      tr.id = out.shots.back().shot_id + "_to_" + inst.shot_id + "_dissolve";
      tr.from_shot_id = out.shots.back().shot_id;
      tr.to_shot_id = inst.shot_id;
      tr.kind = TransitionKind::reveal;
      tr.overlap_frames = spec.dissolve_overlap_frames;
      transition_intents.push_back(std::move(tr));
    }

    out.shots.push_back(std::move(inst));
  }

  if (!transition_intents.empty()) {
    auto transitions = plan_transitions(timings, transition_intents);
    if (!transitions) {
      return std::unexpected(transitions.error());
    }
    out.transitions = std::move(*transitions);
  } else {
    out.transitions.film_duration_frames = out.total_duration_frames;
  }

  // Guardrail: lighting recipe ids must never look like style package names.
  for (const auto &shot : out.shots) {
    if (shot.lighting_recipe_id == "paper-cinema" ||
        shot.lighting_recipe_id == "editorial" ||
        shot.lighting_recipe_id == "scientific-notebook") {
      return std::unexpected(
          make_err("style_name_leak",
                   "Lighting recipe must not use a Style Bible package name"));
    }
    if (!shot.lighting_recipe_id.starts_with("paper_lighting_")) {
      return std::unexpected(make_err(
          "invalid_lighting_recipe",
          "Lighting recipe ids must use the paper_lighting_ feature prefix"));
    }
  }

  return out;
}

std::string format_paper_cinematic_sequence_manifest(
    const PaperCinematicSequenceAssembly &assembly) {
  std::ostringstream os;
  os << "paper_cinematic_sequence\n";
  os << "film_id=" << assembly.film_id << '\n';
  os << "total_frames=" << assembly.total_duration_frames << '\n';
  os << "shots=" << assembly.shots.size() << '\n';
  for (const auto &shot : assembly.shots) {
    os << "shot " << shot.shot_id << " start=" << shot.start_frame
       << " dur=" << shot.duration_frames
       << " intent=" << intent_kind_id(shot.intent)
       << " camera=" << cinematic_camera_preset_id(shot.camera_preset)
       << " lighting=" << shot.lighting_recipe_id << '\n';
  }
  os << "transitions=" << assembly.transitions.spans.size() << '\n';
  for (const auto &span : assembly.transitions.spans) {
    os << "tr " << span.id << " blend=" << span.blend
       << " start=" << span.start_frame << " dur=" << span.duration_frames
       << '\n';
  }
  return os.str();
}

} // namespace instantee::direction
