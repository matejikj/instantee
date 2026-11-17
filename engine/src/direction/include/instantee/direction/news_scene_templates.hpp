#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/editorial_beats.hpp>
#include <instantee/direction/editorial_reels_composition.hpp>
#include <instantee/direction/editorial_transitions.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Reusable editorial news scene templates. Templates compose shared direction
// modules (Reels layout, beats, transition presets) and carry author content
// only — they do not fork the renderer or invent new transition kinds. Style
// Bible tokens supply composition; swapping content rebuilds the reel without
// touching render-core. Audio is intentionally absent in V1 news templates.

enum class NewsSceneKind : std::uint8_t {
  hook = 1,
  hero_claim = 2,
  data_comparison = 3,
  source_screenshot_crop = 4,
  closing_cta = 5,
};

struct NewsComparisonPayload {
  std::string left_label{};
  std::string right_label{};
  // Explicit unit tag consumed later by shared chart primitives
  // ("count" | "percent" | "ratio"). Templates only transport the tag.
  std::string unit{"count"};
  std::vector<double> left_values{};
  std::vector<double> right_values{};
};

struct NewsScreenshotPayload {
  std::string asset_id{};
  double source_width_px = 0;
  double source_height_px = 0;
  // Fit tag for shared photo-crop ("cover" | "contain").
  std::string fit{"cover"};
};

struct NewsSceneContent {
  std::string headline{};
  std::string supporting{};
  std::optional<NewsComparisonPayload> comparison{};
  std::optional<NewsScreenshotPayload> screenshot{};
};

struct NewsSceneSpec {
  NewsSceneKind kind = NewsSceneKind::hook;
  std::uint64_t duration_frames = 0;
  // Existing editorial transition preset into the *next* scene. Unused on the
  // last scene. Templates never rewrite transition math — they only select.
  EditorialTransitionPreset outgoing_transition =
      EditorialTransitionPreset::hard_cut;
  std::uint64_t outgoing_transition_frames = 0;
  TypographicEmphasis outgoing_emphasis = TypographicEmphasis::none;
  NewsSceneContent content{};
};

struct NewsReelTemplateRequest {
  std::string film_id{"news-reel"};
  double canvas_width_px = 1080.0;
  double canvas_height_px = 1920.0;
  // Style-driven composition (safe-area + spacing). Prefer
  // reels_composition_request_from_style when assembling from a Style Bible.
  ReelsCompositionRequest composition{};
  std::vector<NewsSceneSpec> scenes{};
};

struct NewsSceneInstance {
  NewsSceneKind kind = NewsSceneKind::hook;
  std::string shot_id{};
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  EditorialBeatKind beat_kind = EditorialBeatKind::hook;
  IntentKind intent_kind = IntentKind::reveal;
  // Slots activated for this template (subset of Reels composition ids).
  std::vector<std::string> active_slot_ids{};
  ReelsCompositionLayout layout{};
  NewsSceneContent content{};
  EditorialTransitionPreset outgoing_transition =
      EditorialTransitionPreset::hard_cut;
  std::uint64_t outgoing_transition_frames = 0;
  TypographicEmphasis outgoing_emphasis = TypographicEmphasis::none;
};

struct NewsReelAssembly {
  std::string film_id{};
  std::string style_package_id{};
  std::string style_version{};
  std::uint64_t total_duration_frames = 0;
  std::vector<NewsSceneInstance> scenes{};
  EditorialBeatSchedule beat_schedule{};
  // Requests for the existing transition library — not a parallel planner.
  std::vector<EditorialTransitionRequest> transitions{};
  // V1 news templates are silent by design.
  bool has_audio = false;
};

[[nodiscard]] constexpr std::string_view
news_scene_kind_id(NewsSceneKind kind) noexcept {
  switch (kind) {
  case NewsSceneKind::hook:
    return "hook";
  case NewsSceneKind::hero_claim:
    return "hero_claim";
  case NewsSceneKind::data_comparison:
    return "data_comparison";
  case NewsSceneKind::source_screenshot_crop:
    return "source_screenshot_crop";
  case NewsSceneKind::closing_cta:
    return "closing_cta";
  }
  return "unknown";
}

[[nodiscard]] constexpr EditorialBeatKind
news_scene_beat_kind(NewsSceneKind kind) noexcept {
  switch (kind) {
  case NewsSceneKind::hook:
    return EditorialBeatKind::hook;
  case NewsSceneKind::hero_claim:
    return EditorialBeatKind::claim;
  case NewsSceneKind::data_comparison:
  case NewsSceneKind::source_screenshot_crop:
    return EditorialBeatKind::evidence;
  case NewsSceneKind::closing_cta:
    return EditorialBeatKind::conclusion;
  }
  return EditorialBeatKind::hook;
}

[[nodiscard]] constexpr IntentKind
news_scene_intent_kind(NewsSceneKind kind) noexcept {
  switch (kind) {
  case NewsSceneKind::hook:
    return IntentKind::reveal;
  case NewsSceneKind::hero_claim:
    return IntentKind::focus;
  case NewsSceneKind::data_comparison:
    return IntentKind::compare;
  case NewsSceneKind::source_screenshot_crop:
    return IntentKind::explain;
  case NewsSceneKind::closing_cta:
    return IntentKind::build_suspense;
  }
  return IntentKind::reveal;
}

// Default five-scene news reel order with hard cuts (zero-overlap). Durations
// are relative weights packed into `total_duration_frames` by the assembler
// when every spec.duration_frames is 0; otherwise absolute frame counts.
[[nodiscard]] std::vector<NewsSceneSpec>
default_news_reel_scene_specs() noexcept;

// Assemble a silent news reel from style composition + author content. Fails
// closed when required payloads are missing or transitions are invalid for the
// selected preset. Does not call into render-core.
[[nodiscard]] std::expected<NewsReelAssembly, diagnostics::Error>
assemble_news_reel(const NewsReelTemplateRequest &request);

// Convenience: fill composition from a Style Bible, then assemble.
[[nodiscard]] std::expected<NewsReelAssembly, diagnostics::Error>
assemble_news_reel_from_style(const StyleBible &bible,
                              NewsReelTemplateRequest request);

// Parse a news-reel template authoring document (scenes + optional
// variant_content overlays). Style Bible path is returned for the caller to
// load; composition is not filled here.
struct NewsReelAuthoringDocument {
  NewsReelTemplateRequest primary{};
  // Same five-scene kinds with alternate content only (durations/transitions
  // inherited from primary when omitted in the overlay).
  std::vector<NewsSceneContent> variant_contents{};
  std::string style_bible_path{};
};

[[nodiscard]] std::expected<NewsReelAuthoringDocument, diagnostics::Error>
parse_news_reel_authoring(std::string_view json_text);

[[nodiscard]] std::expected<NewsReelAuthoringDocument, diagnostics::Error>
load_news_reel_authoring_file(const std::filesystem::path &path);

// Layout element for a named Reels slot, if present and active on the scene.
[[nodiscard]] const LayoutElement *
news_scene_slot(const NewsSceneInstance &scene, std::string_view slot_id);

} // namespace instantee::direction
