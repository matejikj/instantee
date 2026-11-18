#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/cinematic_camera.hpp>
#include <instantee/direction/shot_planner.hpp>
#include <instantee/direction/transition_planner.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Curated Paper Cinema lighting/shot templates. Each template selects a shared
// lighting recipe id and camera preset — never a Style Bible package name —
// so render-core stays free of style-name branches.
enum class PaperCinematicShotKind : std::uint8_t {
  reveal_stage = 1,
  object_closeup = 2,
  puppet_dialogue = 3,
  technical_explanation = 4,
  atmospheric_dissolve = 5,
};

struct PaperCinematicShotSpec {
  PaperCinematicShotKind kind = PaperCinematicShotKind::reveal_stage;
  std::uint64_t duration_frames = 0;
  // Peak camera intensity in [0, 1].
  double camera_intensity = 0.35;
  // For atmospheric_dissolve: outgoing overlap into the next shot (frames).
  std::uint64_t dissolve_overlap_frames = 0;
};

struct PaperCinematicSequenceRequest {
  std::string film_id{"paper-cinematic-sequence"};
  std::string camera_node_id{"camera"};
  std::vector<PaperCinematicShotSpec> shots{};
};

struct PaperCinematicShotInstance {
  PaperCinematicShotKind kind = PaperCinematicShotKind::reveal_stage;
  std::string shot_id{};
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  IntentKind intent = IntentKind::reveal;
  CameraFraming framing = CameraFraming::static_framing;
  CinematicCameraPreset camera_preset = CinematicCameraPreset::dolly;
  // Stable lighting recipe feature id (render-core), never a package name.
  std::string lighting_recipe_id{};
  ShotPlan plan{};
  std::vector<DirectedTrack> camera_tracks{};
};

struct PaperCinematicSequenceAssembly {
  std::string film_id{};
  std::uint64_t total_duration_frames = 0;
  std::vector<PaperCinematicShotInstance> shots{};
  TransitionPlan transitions{};
};

[[nodiscard]] constexpr std::string_view
paper_cinematic_shot_kind_id(PaperCinematicShotKind kind) noexcept {
  switch (kind) {
  case PaperCinematicShotKind::reveal_stage:
    return "reveal_stage";
  case PaperCinematicShotKind::object_closeup:
    return "object_closeup";
  case PaperCinematicShotKind::puppet_dialogue:
    return "puppet_dialogue";
  case PaperCinematicShotKind::technical_explanation:
    return "technical_explanation";
  case PaperCinematicShotKind::atmospheric_dissolve:
    return "atmospheric_dissolve";
  }
  return "unknown";
}

[[nodiscard]] std::expected<PaperCinematicShotKind, diagnostics::Error>
parse_paper_cinematic_shot_kind(std::string_view text);

// Lighting recipe feature ids consumed by render-core graph assembly.
[[nodiscard]] constexpr std::string_view
paper_cinematic_lighting_recipe_id_for(PaperCinematicShotKind kind) noexcept {
  switch (kind) {
  case PaperCinematicShotKind::reveal_stage:
    return "paper_lighting_reveal_key_fill";
  case PaperCinematicShotKind::object_closeup:
    return "paper_lighting_closeup_rim";
  case PaperCinematicShotKind::puppet_dialogue:
    return "paper_lighting_dialogue_soft";
  case PaperCinematicShotKind::technical_explanation:
    return "paper_lighting_technical_flat";
  case PaperCinematicShotKind::atmospheric_dissolve:
    return "paper_lighting_atmospheric_veil";
  }
  return "unknown";
}

[[nodiscard]] constexpr CinematicCameraPreset
paper_cinematic_camera_preset_for(PaperCinematicShotKind kind) noexcept {
  switch (kind) {
  case PaperCinematicShotKind::reveal_stage:
    return CinematicCameraPreset::dolly;
  case PaperCinematicShotKind::object_closeup:
    return CinematicCameraPreset::slow_zoom;
  case PaperCinematicShotKind::puppet_dialogue:
    return CinematicCameraPreset::truck;
  case PaperCinematicShotKind::technical_explanation:
    return CinematicCameraPreset::rack_depth;
  case PaperCinematicShotKind::atmospheric_dissolve:
    return CinematicCameraPreset::crane_pan;
  }
  return CinematicCameraPreset::dolly;
}

[[nodiscard]] constexpr IntentKind
paper_cinematic_intent_for(PaperCinematicShotKind kind) noexcept {
  switch (kind) {
  case PaperCinematicShotKind::reveal_stage:
    return IntentKind::reveal;
  case PaperCinematicShotKind::object_closeup:
    return IntentKind::focus;
  case PaperCinematicShotKind::puppet_dialogue:
    return IntentKind::compare;
  case PaperCinematicShotKind::technical_explanation:
    return IntentKind::explain;
  case PaperCinematicShotKind::atmospheric_dissolve:
    return IntentKind::build_suspense;
  }
  return IntentKind::reveal;
}

// Assemble the five curated templates (or a validated subset) into a packed
// sequence. Atmospheric dissolve emits a TransitionKind::reveal span.
[[nodiscard]] std::expected<PaperCinematicSequenceAssembly, diagnostics::Error>
assemble_paper_cinematic_sequence(const PaperCinematicSequenceRequest &request);

[[nodiscard]] std::string format_paper_cinematic_sequence_manifest(
    const PaperCinematicSequenceAssembly &assembly);

} // namespace instantee::direction
