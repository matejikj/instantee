#include <instantee/direction/news_scene_templates.hpp>

#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "news_scene_templates", message};
}

[[nodiscard]] bool finite_positive(double v) noexcept {
  return std::isfinite(v) && v > 0.0;
}

[[nodiscard]] std::vector<std::string> active_slots_for(NewsSceneKind kind) {
  switch (kind) {
  case NewsSceneKind::hook:
    return {"top_headline", "callout"};
  case NewsSceneKind::hero_claim:
    return {"top_headline", "callout", "bottom_space"};
  case NewsSceneKind::data_comparison:
    return {"top_headline", "chart", "callout"};
  case NewsSceneKind::source_screenshot_crop:
    return {"top_headline", "chart", "bottom_space"};
  case NewsSceneKind::closing_cta:
    return {"top_headline", "bottom_space"};
  }
  return {};
}

[[nodiscard]] std::optional<diagnostics::Error>
validate_content(const NewsSceneSpec &spec) {
  if (spec.content.headline.empty()) {
    return make_invalid("missing_headline",
                        "Every news scene requires a headline");
  }
  switch (spec.kind) {
  case NewsSceneKind::data_comparison: {
    if (!spec.content.comparison.has_value()) {
      return make_invalid("missing_comparison",
                          "data_comparison requires comparison payload");
    }
    const auto &c = *spec.content.comparison;
    if (c.left_values.empty() || c.right_values.empty()) {
      return make_invalid("empty_comparison_series",
                          "comparison series must be non-empty");
    }
    if (c.left_values.size() != c.right_values.size()) {
      return make_invalid("comparison_length_mismatch",
                          "left/right comparison series must match length");
    }
    if (c.unit != "count" && c.unit != "percent" && c.unit != "ratio") {
      return make_invalid("invalid_comparison_unit",
                          "comparison unit must be count|percent|ratio");
    }
    break;
  }
  case NewsSceneKind::source_screenshot_crop: {
    if (!spec.content.screenshot.has_value()) {
      return make_invalid("missing_screenshot",
                          "source_screenshot_crop requires screenshot payload");
    }
    const auto &s = *spec.content.screenshot;
    if (s.asset_id.empty()) {
      return make_invalid("missing_screenshot_asset",
                          "screenshot asset_id is required");
    }
    if (!finite_positive(s.source_width_px) ||
        !finite_positive(s.source_height_px)) {
      return make_invalid("invalid_screenshot_size",
                          "screenshot source size must be finite and > 0");
    }
    if (s.fit != "cover" && s.fit != "contain") {
      return make_invalid("invalid_screenshot_fit",
                          "screenshot fit must be cover|contain");
    }
    break;
  }
  case NewsSceneKind::closing_cta:
    if (spec.content.supporting.empty()) {
      return make_invalid("missing_cta_label",
                          "closing_cta requires supporting CTA label");
    }
    break;
  case NewsSceneKind::hook:
  case NewsSceneKind::hero_claim:
    break;
  }
  return std::nullopt;
}

[[nodiscard]] bool transition_ok(EditorialTransitionPreset preset,
                                 std::uint64_t frames) noexcept {
  // Matches editorial_transitions: hard_cut and camera_snap are zero-duration.
  if (preset == EditorialTransitionPreset::hard_cut ||
      preset == EditorialTransitionPreset::camera_snap) {
    return frames == 0;
  }
  return frames > 0;
}

[[nodiscard]] ContentSafeRect
type_focus_from_layout(const ReelsCompositionLayout &layout,
                       const std::vector<std::string> &active) {
  ContentSafeRect focus = layout.content;
  for (const auto &id : active) {
    if (id == "top_headline" || id == "callout") {
      for (const auto &el : layout.elements) {
        if (el.id == id) {
          focus.x_px = el.x_px;
          focus.y_px = el.y_px;
          focus.width_px = el.width_px;
          focus.height_px = el.height_px;
          return focus;
        }
      }
    }
  }
  return focus;
}

} // namespace

std::vector<NewsSceneSpec> default_news_reel_scene_specs() noexcept {
  std::vector<NewsSceneSpec> specs(5);
  specs[0].kind = NewsSceneKind::hook;
  specs[0].duration_frames = 120;
  specs[0].outgoing_transition = EditorialTransitionPreset::crop_wipe;
  specs[0].outgoing_transition_frames = 8;
  specs[0].outgoing_emphasis = TypographicEmphasis::mask_push;

  specs[1].kind = NewsSceneKind::hero_claim;
  specs[1].duration_frames = 180;
  specs[1].outgoing_transition = EditorialTransitionPreset::rapid_zoom;
  specs[1].outgoing_transition_frames = 10;
  specs[1].outgoing_emphasis = TypographicEmphasis::slam_in;

  specs[2].kind = NewsSceneKind::data_comparison;
  specs[2].duration_frames = 240;
  specs[2].outgoing_transition = EditorialTransitionPreset::camera_snap;
  specs[2].outgoing_transition_frames = 0;

  specs[3].kind = NewsSceneKind::source_screenshot_crop;
  specs[3].duration_frames = 210;
  specs[3].outgoing_transition = EditorialTransitionPreset::hard_cut;
  specs[3].outgoing_transition_frames = 0;

  specs[4].kind = NewsSceneKind::closing_cta;
  specs[4].duration_frames = 150;
  specs[4].outgoing_transition = EditorialTransitionPreset::hard_cut;
  specs[4].outgoing_transition_frames = 0;
  return specs;
}

std::expected<NewsReelAssembly, diagnostics::Error>
assemble_news_reel(const NewsReelTemplateRequest &request) {
  if (request.film_id.empty()) {
    return std::unexpected(
        make_invalid("missing_film_id", "film_id must be non-empty"));
  }
  if (!finite_positive(request.canvas_width_px) ||
      !finite_positive(request.canvas_height_px)) {
    return std::unexpected(make_invalid(
        "invalid_canvas", "Canvas dimensions must be finite and > 0"));
  }
  if (request.scenes.size() != 5) {
    return std::unexpected(make_invalid(
        "invalid_scene_count",
        "News reel templates require exactly five scenes in order"));
  }

  static constexpr NewsSceneKind kOrder[] = {
      NewsSceneKind::hook, NewsSceneKind::hero_claim,
      NewsSceneKind::data_comparison, NewsSceneKind::source_screenshot_crop,
      NewsSceneKind::closing_cta};

  EditorialBeatScheduleRequest beat_req;
  beat_req.beats.reserve(5);
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < request.scenes.size(); ++i) {
    const auto &spec = request.scenes[i];
    if (spec.kind != kOrder[i]) {
      return std::unexpected(
          make_invalid("invalid_scene_order",
                       "Scenes must be hook → hero_claim → data_comparison → "
                       "source_screenshot_crop → closing_cta"));
    }
    if (spec.duration_frames == 0) {
      return std::unexpected(make_invalid(
          "invalid_duration", "Each scene duration_frames must be > 0"));
    }
    if (auto err = validate_content(spec)) {
      return std::unexpected(std::move(*err));
    }
    const bool last = i + 1 == request.scenes.size();
    if (!last && !transition_ok(spec.outgoing_transition,
                                spec.outgoing_transition_frames)) {
      return std::unexpected(make_invalid(
          "invalid_outgoing_transition",
          "Transition duration must match the selected preset rules"));
    }
    total += spec.duration_frames;

    EditorialBeatMarker marker;
    marker.kind = news_scene_beat_kind(spec.kind);
    marker.weight = spec.duration_frames;
    marker.track_id = std::string(news_scene_kind_id(spec.kind)) + "_gate";
    marker.target_id = std::string(news_scene_kind_id(spec.kind));
    marker.property = "opacity";
    beat_req.beats.push_back(std::move(marker));
  }
  beat_req.total_duration_frames = total;

  auto beats = schedule_editorial_beats(beat_req);
  if (!beats) {
    return std::unexpected(beats.error());
  }

  ReelsCompositionRequest composition = request.composition;
  composition.canvas_width_px = request.canvas_width_px;
  composition.canvas_height_px = request.canvas_height_px;
  auto layout = compose_editorial_reels(composition);
  if (!layout) {
    return std::unexpected(layout.error());
  }
  if (!reels_composition_respects_safe_area(*layout)) {
    return std::unexpected(
        make_invalid("layout_outside_safe_area",
                     "Reels composition escaped the content-safe rect"));
  }

  NewsReelAssembly out;
  out.film_id = request.film_id;
  out.style_package_id = composition.style_package_id;
  out.style_version = composition.style_version;
  out.total_duration_frames = total;
  out.beat_schedule = std::move(*beats);
  out.has_audio = false;
  out.scenes.reserve(5);

  std::uint64_t cursor = 0;
  for (std::size_t i = 0; i < request.scenes.size(); ++i) {
    const auto &spec = request.scenes[i];
    NewsSceneInstance scene;
    scene.kind = spec.kind;
    scene.shot_id = std::string(news_scene_kind_id(spec.kind));
    scene.start_frame = cursor;
    scene.duration_frames = spec.duration_frames;
    scene.beat_kind = news_scene_beat_kind(spec.kind);
    scene.intent_kind = news_scene_intent_kind(spec.kind);
    scene.active_slot_ids = active_slots_for(spec.kind);
    scene.layout = *layout;
    scene.content = spec.content;
    scene.outgoing_transition = spec.outgoing_transition;
    scene.outgoing_transition_frames = spec.outgoing_transition_frames;
    scene.outgoing_emphasis = spec.outgoing_emphasis;
    cursor += spec.duration_frames;

    if (i + 1 < request.scenes.size()) {
      EditorialTransitionRequest tr;
      tr.id = scene.shot_id + "_to_" +
              std::string(news_scene_kind_id(request.scenes[i + 1].kind));
      tr.from_shot_id = scene.shot_id;
      tr.to_shot_id =
          std::string(news_scene_kind_id(request.scenes[i + 1].kind));
      tr.preset = spec.outgoing_transition;
      tr.emphasis = spec.outgoing_emphasis;
      tr.duration_frames = spec.outgoing_transition_frames;
      tr.intensity = 0.75;
      tr.safe_area = composition.safe_area;
      tr.canvas_width_px = request.canvas_width_px;
      tr.canvas_height_px = request.canvas_height_px;
      tr.type_focus = type_focus_from_layout(*layout, scene.active_slot_ids);
      // Validate against the existing library without forking it.
      auto program = compile_editorial_transition(tr);
      if (!program) {
        return std::unexpected(program.error());
      }
      out.transitions.push_back(std::move(tr));
    }

    out.scenes.push_back(std::move(scene));
  }

  if (cursor != total) {
    return std::unexpected(make_invalid(
        "duration_mismatch", "Packed scene durations drifted from total"));
  }
  return out;
}

std::expected<NewsReelAssembly, diagnostics::Error>
assemble_news_reel_from_style(const StyleBible &bible,
                              NewsReelTemplateRequest request) {
  request.composition = reels_composition_request_from_style(
      bible, request.canvas_width_px, request.canvas_height_px);
  return assemble_news_reel(request);
}

namespace {

[[nodiscard]] std::optional<NewsSceneKind>
parse_kind(std::string_view id) noexcept {
  if (id == "hook") {
    return NewsSceneKind::hook;
  }
  if (id == "hero_claim") {
    return NewsSceneKind::hero_claim;
  }
  if (id == "data_comparison") {
    return NewsSceneKind::data_comparison;
  }
  if (id == "source_screenshot_crop") {
    return NewsSceneKind::source_screenshot_crop;
  }
  if (id == "closing_cta") {
    return NewsSceneKind::closing_cta;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<EditorialTransitionPreset>
parse_transition(std::string_view id) noexcept {
  if (id == "hard_cut") {
    return EditorialTransitionPreset::hard_cut;
  }
  if (id == "crop_wipe") {
    return EditorialTransitionPreset::crop_wipe;
  }
  if (id == "rapid_zoom") {
    return EditorialTransitionPreset::rapid_zoom;
  }
  if (id == "camera_snap") {
    return EditorialTransitionPreset::camera_snap;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<TypographicEmphasis>
parse_emphasis(std::string_view id) noexcept {
  if (id == "none") {
    return TypographicEmphasis::none;
  }
  if (id == "slam_in") {
    return TypographicEmphasis::slam_in;
  }
  if (id == "mask_push") {
    return TypographicEmphasis::mask_push;
  }
  return std::nullopt;
}

[[nodiscard]] std::expected<std::vector<double>, diagnostics::Error>
parse_number_array(yyjson_val *arr, std::string_view path) {
  if (arr == nullptr || !yyjson_is_arr(arr)) {
    return std::unexpected(
        make_invalid("invalid_array", std::string(path) + " must be an array"));
  }
  std::vector<double> out;
  out.reserve(yyjson_arr_size(arr));
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(arr, &iter);
  while (yyjson_val *item = yyjson_arr_iter_next(&iter)) {
    if (!yyjson_is_num(item)) {
      return std::unexpected(make_invalid(
          "invalid_number", std::string(path) + " entries must be numbers"));
    }
    out.push_back(yyjson_get_num(item));
  }
  return out;
}

[[nodiscard]] std::expected<NewsSceneContent, diagnostics::Error>
parse_content(yyjson_val *obj, std::string_view path) {
  if (obj == nullptr || !yyjson_is_obj(obj)) {
    return std::unexpected(
        make_invalid("invalid_content", std::string(path) + " must be object"));
  }
  NewsSceneContent content;
  yyjson_val *headline = yyjson_obj_get(obj, "headline");
  yyjson_val *supporting = yyjson_obj_get(obj, "supporting");
  if (headline == nullptr || !yyjson_is_str(headline)) {
    return std::unexpected(make_invalid(
        "missing_headline", std::string(path) + "/headline required"));
  }
  content.headline = yyjson_get_str(headline);
  if (supporting != nullptr) {
    if (!yyjson_is_str(supporting)) {
      return std::unexpected(make_invalid(
          "invalid_supporting", std::string(path) + "/supporting must be str"));
    }
    content.supporting = yyjson_get_str(supporting);
  }

  if (yyjson_val *cmp = yyjson_obj_get(obj, "comparison")) {
    if (!yyjson_is_obj(cmp)) {
      return std::unexpected(make_invalid(
          "invalid_comparison", std::string(path) + "/comparison object"));
    }
    NewsComparisonPayload payload;
    if (yyjson_val *v = yyjson_obj_get(cmp, "left_label");
        v && yyjson_is_str(v)) {
      payload.left_label = yyjson_get_str(v);
    }
    if (yyjson_val *v = yyjson_obj_get(cmp, "right_label");
        v && yyjson_is_str(v)) {
      payload.right_label = yyjson_get_str(v);
    }
    if (yyjson_val *v = yyjson_obj_get(cmp, "unit"); v && yyjson_is_str(v)) {
      payload.unit = yyjson_get_str(v);
    }
    auto left =
        parse_number_array(yyjson_obj_get(cmp, "left_values"),
                           std::string(path) + "/comparison/left_values");
    if (!left) {
      return std::unexpected(left.error());
    }
    auto right =
        parse_number_array(yyjson_obj_get(cmp, "right_values"),
                           std::string(path) + "/comparison/right_values");
    if (!right) {
      return std::unexpected(right.error());
    }
    payload.left_values = std::move(*left);
    payload.right_values = std::move(*right);
    content.comparison = std::move(payload);
  }

  if (yyjson_val *shot = yyjson_obj_get(obj, "screenshot")) {
    if (!yyjson_is_obj(shot)) {
      return std::unexpected(make_invalid(
          "invalid_screenshot", std::string(path) + "/screenshot object"));
    }
    NewsScreenshotPayload payload;
    if (yyjson_val *v = yyjson_obj_get(shot, "asset_id");
        v && yyjson_is_str(v)) {
      payload.asset_id = yyjson_get_str(v);
    }
    if (yyjson_val *v = yyjson_obj_get(shot, "source_width_px");
        v && yyjson_is_num(v)) {
      payload.source_width_px = yyjson_get_num(v);
    }
    if (yyjson_val *v = yyjson_obj_get(shot, "source_height_px");
        v && yyjson_is_num(v)) {
      payload.source_height_px = yyjson_get_num(v);
    }
    if (yyjson_val *v = yyjson_obj_get(shot, "fit"); v && yyjson_is_str(v)) {
      payload.fit = yyjson_get_str(v);
    }
    content.screenshot = std::move(payload);
  }
  return content;
}

[[nodiscard]] std::expected<NewsSceneSpec, diagnostics::Error>
parse_scene_spec(yyjson_val *obj, std::string_view path) {
  if (obj == nullptr || !yyjson_is_obj(obj)) {
    return std::unexpected(
        make_invalid("invalid_scene", std::string(path) + " must be object"));
  }
  NewsSceneSpec spec;
  yyjson_val *kind_v = yyjson_obj_get(obj, "kind");
  if (kind_v == nullptr || !yyjson_is_str(kind_v)) {
    return std::unexpected(
        make_invalid("missing_kind", std::string(path) + "/kind required"));
  }
  auto kind = parse_kind(yyjson_get_str(kind_v));
  if (!kind) {
    return std::unexpected(
        make_invalid("unknown_kind", std::string(path) + "/kind unsupported"));
  }
  spec.kind = *kind;

  if (yyjson_val *d = yyjson_obj_get(obj, "duration_frames");
      d && yyjson_is_int(d)) {
    const int64_t frames = yyjson_get_sint(d);
    if (frames <= 0) {
      return std::unexpected(make_invalid(
          "invalid_duration", std::string(path) + "/duration_frames > 0"));
    }
    spec.duration_frames = static_cast<std::uint64_t>(frames);
  }

  if (yyjson_val *t = yyjson_obj_get(obj, "outgoing_transition");
      t && yyjson_is_str(t)) {
    auto preset = parse_transition(yyjson_get_str(t));
    if (!preset) {
      return std::unexpected(
          make_invalid("unknown_transition",
                       std::string(path) + "/outgoing_transition unsupported"));
    }
    spec.outgoing_transition = *preset;
  }
  if (yyjson_val *d = yyjson_obj_get(obj, "outgoing_transition_frames");
      d && yyjson_is_int(d)) {
    const int64_t frames = yyjson_get_sint(d);
    if (frames < 0) {
      return std::unexpected(
          make_invalid("invalid_transition_frames",
                       std::string(path) + "/outgoing_transition_frames >= 0"));
    }
    spec.outgoing_transition_frames = static_cast<std::uint64_t>(frames);
  }
  if (yyjson_val *e = yyjson_obj_get(obj, "outgoing_emphasis");
      e && yyjson_is_str(e)) {
    auto emph = parse_emphasis(yyjson_get_str(e));
    if (!emph) {
      return std::unexpected(
          make_invalid("unknown_emphasis",
                       std::string(path) + "/outgoing_emphasis unsupported"));
    }
    spec.outgoing_emphasis = *emph;
  }

  auto content = parse_content(yyjson_obj_get(obj, "content"),
                               std::string(path) + "/content");
  if (!content) {
    return std::unexpected(content.error());
  }
  spec.content = std::move(*content);
  return spec;
}

} // namespace

std::expected<NewsReelAuthoringDocument, diagnostics::Error>
parse_news_reel_authoring(std::string_view json_text) {
  yyjson_doc *doc =
      yyjson_read(json_text.data(), json_text.size(), YYJSON_READ_NOFLAG);
  if (doc == nullptr) {
    return std::unexpected(
        make_invalid("invalid_json", "Authoring document is not valid JSON"));
  }
  struct Guard {
    yyjson_doc *owned = nullptr;
    ~Guard() { yyjson_doc_free(owned); }
  } guard{doc};

  yyjson_val *root = yyjson_doc_get_root(doc);
  if (root == nullptr || !yyjson_is_obj(root)) {
    return std::unexpected(
        make_invalid("invalid_root", "Authoring root must be an object"));
  }

  yyjson_val *schema = yyjson_obj_get(root, "schema_version");
  if (schema == nullptr || !yyjson_is_str(schema) ||
      std::string_view{yyjson_get_str(schema)} != "news-reel-template/1") {
    return std::unexpected(make_invalid(
        "unsupported_schema", "schema_version must be news-reel-template/1"));
  }

  NewsReelAuthoringDocument out;
  if (yyjson_val *id = yyjson_obj_get(root, "film_id");
      id && yyjson_is_str(id)) {
    out.primary.film_id = yyjson_get_str(id);
  } else {
    return std::unexpected(
        make_invalid("missing_film_id", "film_id is required"));
  }
  if (yyjson_val *style = yyjson_obj_get(root, "style_bible");
      style && yyjson_is_str(style)) {
    out.style_bible_path = yyjson_get_str(style);
  } else {
    return std::unexpected(
        make_invalid("missing_style_bible", "style_bible path is required"));
  }

  yyjson_val *canvas = yyjson_obj_get(root, "canvas");
  if (canvas == nullptr || !yyjson_is_obj(canvas)) {
    return std::unexpected(
        make_invalid("missing_canvas", "canvas object is required"));
  }
  yyjson_val *w = yyjson_obj_get(canvas, "width_px");
  yyjson_val *h = yyjson_obj_get(canvas, "height_px");
  if (w == nullptr || h == nullptr || !yyjson_is_num(w) || !yyjson_is_num(h)) {
    return std::unexpected(make_invalid(
        "invalid_canvas", "canvas width_px/height_px must be numbers"));
  }
  out.primary.canvas_width_px = yyjson_get_num(w);
  out.primary.canvas_height_px = yyjson_get_num(h);

  yyjson_val *scenes = yyjson_obj_get(root, "scenes");
  if (scenes == nullptr || !yyjson_is_arr(scenes) ||
      yyjson_arr_size(scenes) != 5) {
    return std::unexpected(make_invalid(
        "invalid_scenes", "scenes must be an array of exactly five entries"));
  }
  out.primary.scenes.reserve(5);
  std::size_t index = 0;
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(scenes, &iter);
  while (yyjson_val *item = yyjson_arr_iter_next(&iter)) {
    auto spec = parse_scene_spec(item, "/scenes/" + std::to_string(index));
    if (!spec) {
      return std::unexpected(spec.error());
    }
    if (spec->duration_frames == 0) {
      return std::unexpected(make_invalid(
          "missing_duration", "primary scenes require duration_frames"));
    }
    out.primary.scenes.push_back(std::move(*spec));
    ++index;
  }

  if (yyjson_val *variant = yyjson_obj_get(root, "variant_content")) {
    if (!yyjson_is_obj(variant)) {
      return std::unexpected(
          make_invalid("invalid_variant", "variant_content must be an object"));
    }
    yyjson_val *vs = yyjson_obj_get(variant, "scenes");
    if (vs == nullptr || !yyjson_is_arr(vs) || yyjson_arr_size(vs) != 5) {
      return std::unexpected(
          make_invalid("invalid_variant_scenes",
                       "variant_content.scenes must have five entries"));
    }
    out.variant_contents.reserve(5);
    std::size_t vi = 0;
    yyjson_arr_iter viter;
    yyjson_arr_iter_init(vs, &viter);
    while (yyjson_val *item = yyjson_arr_iter_next(&viter)) {
      if (!yyjson_is_obj(item)) {
        return std::unexpected(make_invalid("invalid_variant_scene",
                                            "variant scene must be object"));
      }
      yyjson_val *kind_v = yyjson_obj_get(item, "kind");
      if (kind_v == nullptr || !yyjson_is_str(kind_v)) {
        return std::unexpected(
            make_invalid("missing_variant_kind", "variant kind required"));
      }
      auto kind = parse_kind(yyjson_get_str(kind_v));
      if (!kind || *kind != out.primary.scenes[vi].kind) {
        return std::unexpected(make_invalid(
            "variant_kind_mismatch",
            "variant scene kinds must match the primary template order"));
      }
      auto content = parse_content(yyjson_obj_get(item, "content"),
                                   "/variant_content/scenes/" +
                                       std::to_string(vi) + "/content");
      if (!content) {
        return std::unexpected(content.error());
      }
      out.variant_contents.push_back(std::move(*content));
      ++vi;
    }
  }
  return out;
}

std::expected<NewsReelAuthoringDocument, diagnostics::Error>
load_news_reel_authoring_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::unexpected(make_invalid(
        "authoring_unreadable", "Unable to read news reel authoring file"));
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_news_reel_authoring(ss.str());
}

const LayoutElement *news_scene_slot(const NewsSceneInstance &scene,
                                     std::string_view slot_id) {
  const bool active =
      std::find(scene.active_slot_ids.begin(), scene.active_slot_ids.end(),
                slot_id) != scene.active_slot_ids.end();
  if (!active) {
    return nullptr;
  }
  for (const auto &el : scene.layout.elements) {
    if (el.id == slot_id) {
      return &el;
    }
  }
  return nullptr;
}

} // namespace instantee::direction
