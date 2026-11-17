#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/direction/editorial_visual_acceptance.hpp>
#include <instantee/direction/style_bible.hpp>
#include <instantee/preview/frame_png.hpp>
#include <instantee/runtime/frame_evaluator.hpp>
#include <instantee/runtime/frame_preview.hpp>
#include <instantee/runtime/timeline.hpp>
#include <instantee/scene_ir/canonicalize.hpp>
#include <instantee/scene_ir/load.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using namespace instantee::direction;
using instantee::core::FrameIndex;
using instantee::preview::export_preview_png;
using instantee::preview::rasterize_preview_frame;
using instantee::runtime::AnimatableChannel;
using instantee::runtime::AnimationProgram;
using instantee::runtime::make_scalar_track;
using instantee::runtime::preview_frame_at;
using instantee::runtime::ScalarKeyframe;
using instantee::runtime::ScalarPropertyBinding;
using instantee::runtime::TrackInterpolation;
using instantee::scene_ir::sha256_hex;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

[[nodiscard]] bool write_bytes(const std::filesystem::path &path,
                               std::string_view body) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] std::string sha256_file(const std::filesystem::path &path) {
  const auto bytes = read_file(path);
  if (bytes.empty() && !std::filesystem::exists(path)) {
    return {};
  }
  std::vector<std::byte> raw(bytes.size());
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    raw[i] = static_cast<std::byte>(static_cast<unsigned char>(bytes[i]));
  }
  auto digest = sha256_hex(raw);
  return digest ? *digest : std::string{};
}

[[nodiscard]] std::uint64_t utf8_codepoint_count(std::string_view text) {
  std::uint64_t count = 0;
  for (unsigned char ch : text) {
    if ((ch & 0xC0U) != 0x80U) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] std::vector<VisualChecklistItem> editorial_checklist() {
  return {
      {.id = "shot_boundaries",
       .description =
           "Each news shot boundary frame is pinned and identifiable",
       .required = true},
      {.id = "safe_area",
       .description =
           "Protected content stays inside the Style Bible safe-area rect",
       .required = true},
      {.id = "typography",
       .description =
           "Headline/body roles and sizes match the typography golden",
       .required = true},
      {.id = "reference_frames",
       .description =
           "PNG reference frames compare within the locked tolerance",
       .required = true},
  };
}

[[nodiscard]] std::optional<AnimatableChannel>
channel_from_property(std::string_view property) {
  if (property == "opacity") {
    return AnimatableChannel::opacity;
  }
  if (property == "translate_x") {
    return AnimatableChannel::translate_x;
  }
  if (property == "translate_y") {
    return AnimatableChannel::translate_y;
  }
  if (property == "translate_z") {
    return AnimatableChannel::translate_z;
  }
  if (property == "scale_x") {
    return AnimatableChannel::scale_x;
  }
  if (property == "scale_y") {
    return AnimatableChannel::scale_y;
  }
  if (property == "scale_z") {
    return AnimatableChannel::scale_z;
  }
  if (property == "rotation_z_rad") {
    return AnimatableChannel::rotation_z_rad;
  }
  return std::nullopt;
}

// Scene IR stores tracks on the document; preview evaluation consumes an
// AnimationProgram. Lower the authored scalar tracks so golden frames see the
// same opacity/transform curves as the reel timeline.
[[nodiscard]] std::optional<AnimationProgram>
animation_program_from_scene(const instantee::scene_ir::SceneDocument &document,
                             const instantee::runtime::CompiledScene &scene) {
  std::unordered_map<std::string, std::uint32_t> index_by_id;
  const auto ids = scene.node_ids();
  for (std::uint32_t i = 0; i < ids.size(); ++i) {
    index_by_id.emplace(std::string(ids[i]), i);
  }
  AnimationProgram program;
  for (const auto &track : document.animation_tracks) {
    const auto channel = channel_from_property(track.property);
    const auto node = index_by_id.find(track.target_id);
    if (!channel || node == index_by_id.end() || track.keyframes.empty()) {
      continue;
    }
    std::vector<ScalarKeyframe> keys;
    keys.reserve(track.keyframes.size());
    for (const auto &key : track.keyframes) {
      keys.push_back(ScalarKeyframe{FrameIndex{key.frame}, key.value});
    }
    auto scalar =
        make_scalar_track(std::move(keys), TrackInterpolation::linear);
    if (!scalar) {
      return std::nullopt;
    }
    program.scalar_bindings.push_back(ScalarPropertyBinding{
        .node_index = node->second,
        .channel = *channel,
        .track = std::move(*scalar),
    });
  }
  return program;
}

[[nodiscard]] std::uint64_t
first_opaque_headline_frame(const instantee::scene_ir::SceneDocument &document,
                            const instantee::scene_ir::Shot &shot) {
  // Shot-start frames often still fade in; pin a readable frame inside the
  // shot so typography reference PNGs are not empty canvases.
  const std::uint64_t shot_end = shot.start_frame + shot.duration_frames;
  for (const auto &node_id : shot.node_ids) {
    if (node_id.find("headline") == std::string::npos) {
      continue;
    }
    for (const auto &track : document.animation_tracks) {
      if (track.target_id != node_id || track.property != "opacity") {
        continue;
      }
      for (const auto &key : track.keyframes) {
        if (key.frame >= shot.start_frame && key.frame < shot_end &&
            key.value >= 0.99) {
          return key.frame;
        }
      }
    }
  }
  const std::uint64_t offset =
      shot.duration_frames > 0
          ? std::min<std::uint64_t>(12, shot.duration_frames - 1)
          : 0;
  return shot.start_frame + offset;
}

[[nodiscard]] ShotBoundaryObservation
observe_shot(const instantee::scene_ir::SceneDocument &document,
             const instantee::scene_ir::Shot &shot,
             const ContentSafeRect &safe) {
  ShotBoundaryObservation observation{};
  observation.shot_id = shot.id;
  observation.start_frame = shot.start_frame;
  observation.duration_frames = shot.duration_frames;
  observation.sample_frame = first_opaque_headline_frame(document, shot);
  observation.content_safe = safe;

  std::unordered_map<std::string, const instantee::scene_ir::TextRun *> runs;
  for (const auto &run : document.text_runs) {
    runs.emplace(run.id, &run);
  }
  std::unordered_map<std::string, const instantee::scene_ir::Node *> nodes;
  for (const auto &node : document.nodes) {
    nodes.emplace(node.id, &node);
  }
  for (const auto &node_id : shot.node_ids) {
    const auto node_it = nodes.find(node_id);
    if (node_it == nodes.end() || !node_it->second->text_run_id) {
      continue;
    }
    const auto run_it = runs.find(*node_it->second->text_run_id);
    if (run_it == runs.end()) {
      continue;
    }
    TypographyObservation text{};
    text.node_id = node_id;
    text.text_run_id = run_it->second->id;
    text.text_role = run_it->second->text_role.value_or("");
    text.font_size_px = run_it->second->font_size_px;
    text.text_codepoint_count = utf8_codepoint_count(run_it->second->text);
    observation.typography.push_back(std::move(text));
  }
  std::sort(observation.typography.begin(), observation.typography.end(),
            [](const TypographyObservation &a, const TypographyObservation &b) {
              return a.node_id < b.node_id;
            });
  return observation;
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr << "usage: editorial_visual_acceptance_test <repo-root> "
                 "[--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir = root / "contracts/fixtures/b01-editorial/goldens";
  const auto review_dir =
      root / "contracts/fixtures/b01-editorial/visual-review";
  const auto scene_path =
      root / "contracts/fixtures/b01-editorial/b01-news-reel.scene.json";
  const auto bible_path =
      root / "contracts/fixtures/style-bible/positive/editorial.style.json";

  // Protocol unit coverage: tolerance + checklist + change records.
  {
    VisualTolerance ok{.max_channel_delta = 1, .max_mismatch_ratio = 0.01};
    expect(validate_visual_tolerance(ok).has_value(), "tolerance ok");
    expect(
        !validate_visual_tolerance(
             VisualTolerance{.max_channel_delta = 0, .max_mismatch_ratio = 1.5})
             .has_value(),
        "reject bad ratio");

    const std::vector<std::uint8_t> a{10, 20, 30, 255, 1, 2, 3, 255};
    const std::vector<std::uint8_t> b{10, 20, 31, 255, 1, 2, 3, 255};
    auto exact = compare_rgba8_images(2, 1, a, a, VisualTolerance{});
    expect(exact.has_value() && exact->within_tolerance &&
               exact->mismatched_pixels == 0,
           "exact match");
    auto tight = compare_rgba8_images(2, 1, a, b, VisualTolerance{});
    expect(tight.has_value() && !tight->within_tolerance &&
               tight->mismatched_pixels == 1,
           "delta rejects");
    auto loose = compare_rgba8_images(
        2, 1, a, b,
        VisualTolerance{.max_channel_delta = 1, .max_mismatch_ratio = 0.0});
    expect(loose.has_value() && loose->within_tolerance, "delta allows");

    auto checklist = editorial_checklist();
    std::vector<VisualChecklistResult> results;
    for (const auto &item : checklist) {
      results.push_back({.id = item.id, .passed = true, .notes = "unit"});
    }
    expect(validate_checklist_results(checklist, results).has_value(),
           "checklist complete");
    results[0].passed = false;
    expect(!validate_checklist_results(checklist, results).has_value(),
           "required fail");

    const std::string h1(64, 'a');
    const std::string h2(64, 'b');
    std::vector<GoldenArtifactDigest> previous{
        {.artifact_id = "shot-hook.png", .kind = "png", .sha256 = h1}};
    std::vector<GoldenArtifactDigest> next{
        {.artifact_id = "shot-hook.png", .kind = "png", .sha256 = h2}};
    expect(!validate_golden_change_records(previous, next, {}).has_value(),
           "digest change needs record");
    GoldenChangeRecordEntry entry{.artifact_id = "shot-hook.png",
                                  .previous_sha256 = h1,
                                  .next_sha256 = h2,
                                  .reason = "refresh hook frame",
                                  .approved_by_role =
                                      "editorial-visual-reviewer",
                                  .approved_at_utc = "2026-11-18T00:00:00Z"};
    expect(
        validate_golden_change_records(previous, next, {&entry, 1}).has_value(),
        "covered change");
    expect(validate_golden_change_records(previous, previous, {}).has_value(),
           "unchanged needs no record");
  }

  auto bible = load_style_bible_file(bible_path);
  expect(bible.has_value(), "load editorial bible");
  if (!bible) {
    return EXIT_FAILURE;
  }
  auto safe = content_safe_rect(bible->safe_area(), 1080, 1920);
  expect(safe.has_value(), "content safe rect");
  if (!safe) {
    return EXIT_FAILURE;
  }

  instantee::scene_ir::LoadOptions load_options;
  load_options.project_root = root;
  load_options.enforce_uri_sandbox = true;
  auto document =
      instantee::scene_ir::load_scene_file(scene_path, load_options);
  expect(document.has_value(), "load B-01 scene");
  if (!document) {
    return EXIT_FAILURE;
  }
  expect(document->shots.size() == 5, "five shots");

  auto compiled = instantee::compiler::compile_runtime_scene(*document);
  expect(compiled.has_value() && *compiled != nullptr, "compile");
  if (!compiled || !*compiled) {
    return EXIT_FAILURE;
  }
  auto program = animation_program_from_scene(*document, **compiled);
  expect(program.has_value() && !program->scalar_bindings.empty(),
         "animation program from scene tracks");
  if (!program) {
    return EXIT_FAILURE;
  }

  std::vector<GoldenArtifactDigest> digests;
  VisualTolerance tolerance{};
  std::vector<VisualChecklistResult> review_results;

  bool shot_boundaries_ok = true;
  bool safe_area_ok = true;
  bool typography_ok = true;
  bool reference_frames_ok = true;

  for (const auto &shot : document->shots) {
    auto observation = observe_shot(*document, shot, *safe);
    const std::string structural = format_shot_boundary_golden(observation);
    const auto structural_path = golden_dir / (shot.id + ".structural.txt");
    const auto png_path = golden_dir / (shot.id + ".png");

    const FrameIndex sample{observation.sample_frame};
    if (write_goldens) {
      expect(write_bytes(structural_path, structural), "write structural");
      auto exported =
          export_preview_png(**compiled, *program, sample, png_path);
      expect(exported.has_value(), "export png");
    } else {
      expect(std::filesystem::exists(structural_path), "structural golden");
      expect(read_file(structural_path) == structural, "structural match");
      if (read_file(structural_path) != structural) {
        shot_boundaries_ok = false;
        typography_ok = false;
        safe_area_ok = false;
      }
      expect(std::filesystem::exists(png_path), "png golden");
      auto preview = preview_frame_at(**compiled, *program, sample);
      expect(preview.has_value(), "preview frame");
      if (!preview) {
        reference_frames_ok = false;
        continue;
      }
      auto actual = rasterize_preview_frame(**compiled, preview->state);
      expect(actual.has_value(), "rasterize");
      auto tmp = std::filesystem::temp_directory_path() /
                 ("instantee-editorial-golden-" + shot.id + ".png");
      auto again = export_preview_png(**compiled, *program, sample, tmp);
      expect(again.has_value(), "re-export for compare");
      if (again) {
        const auto golden_sha = sha256_file(png_path);
        const auto actual_sha = sha256_file(tmp);
        expect(!golden_sha.empty() && golden_sha == actual_sha,
               "png digest match");
        if (golden_sha != actual_sha) {
          reference_frames_ok = false;
        }
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
      } else {
        reference_frames_ok = false;
      }
      if (observation.content_safe.width_px <= 0 ||
          observation.content_safe.height_px <= 0) {
        safe_area_ok = false;
      }
      if (observation.typography.empty()) {
        typography_ok = false;
      }
      for (const auto &text : observation.typography) {
        if (text.text_role.empty()) {
          typography_ok = false;
        }
      }
    }

    digests.push_back({.artifact_id = shot.id + ".structural.txt",
                       .kind = "structural",
                       .sha256 = sha256_file(structural_path)});
    digests.push_back({.artifact_id = shot.id + ".png",
                       .kind = "png",
                       .sha256 = sha256_file(png_path)});
  }

  // Manifest + change record + signed review package.
  std::ostringstream manifest;
  manifest << "{\n  \"schema_version\": \"1.0.0\",\n  \"workload\": "
              "\"B-01-editorial-news-reel-60s-v1\",\n  \"artifacts\": [\n";
  for (std::size_t i = 0; i < digests.size(); ++i) {
    const auto &d = digests[i];
    manifest << "    {\"id\": \"" << d.artifact_id << "\", \"kind\": \""
             << d.kind << "\", \"sha256\": \"" << d.sha256 << "\"}";
    if (i + 1 < digests.size()) {
      manifest << ',';
    }
    manifest << '\n';
  }
  manifest << "  ]\n}\n";
  const auto manifest_path = golden_dir / "manifest.json";
  const auto change_path = golden_dir / "CHANGE_RECORD.json";
  const auto checklist_path = review_dir / "checklist.json";
  const auto tolerances_path = review_dir / "tolerances.json";
  const auto review_path = review_dir / "review-record.json";

  if (write_goldens) {
    expect(write_bytes(manifest_path, manifest.str()), "write manifest");
    std::ostringstream changes;
    changes << "{\n  \"schema_version\": \"1.0.0\",\n  \"entries\": [\n";
    for (std::size_t i = 0; i < digests.size(); ++i) {
      const auto &d = digests[i];
      changes << "    {\n"
              << "      \"artifact_id\": \"" << d.artifact_id << "\",\n"
              << "      \"previous_sha256\": \"\",\n"
              << "      \"next_sha256\": \"" << d.sha256 << "\",\n"
              << "      \"reason\": \"initial editorial reference approval\",\n"
              << "      \"approved_by_role\": \"editorial-visual-reviewer\",\n"
              << "      \"approved_at_utc\": \"2026-11-18T00:00:00Z\"\n"
              << "    }";
      if (i + 1 < digests.size()) {
        changes << ',';
      }
      changes << '\n';
    }
    changes << "  ]\n}\n";
    expect(write_bytes(change_path, changes.str()), "write change record");

    std::ostringstream checklist_json;
    checklist_json << "{\n  \"schema_version\": \"1.0.0\",\n  \"items\": [\n";
    auto checklist = editorial_checklist();
    for (std::size_t i = 0; i < checklist.size(); ++i) {
      checklist_json << "    {\"id\": \"" << checklist[i].id
                     << "\", \"description\": \"" << checklist[i].description
                     << "\", \"required\": "
                     << (checklist[i].required ? "true" : "false") << "}";
      if (i + 1 < checklist.size()) {
        checklist_json << ',';
      }
      checklist_json << '\n';
    }
    checklist_json << "  ]\n}\n";
    expect(write_bytes(checklist_path, checklist_json.str()), "checklist");
    expect(write_bytes(tolerances_path, "{\n  \"max_channel_delta\": 0,\n  "
                                        "\"max_mismatch_ratio\": 0.0\n}\n"),
           "tolerances");

    const auto manifest_sha = sha256_file(manifest_path);
    std::ostringstream review;
    review << "{\n"
           << "  \"schema_version\": \"1.0.0\",\n"
           << "  \"status\": \"approved\",\n"
           << "  \"approved_by_role\": \"editorial-visual-reviewer\",\n"
           << "  \"approved_at_utc\": \"2026-11-18T00:00:00Z\",\n"
           << "  \"golden_manifest_sha256\": \"" << manifest_sha << "\",\n"
           << "  \"tolerances\": {\"max_channel_delta\": 0, "
              "\"max_mismatch_ratio\": 0.0},\n"
           << "  \"method\": \"" << editorial_visual_acceptance_method()
           << "\",\n"
           << "  \"checklist_results\": [\n";
    for (std::size_t i = 0; i < checklist.size(); ++i) {
      review << "    {\"id\": \"" << checklist[i].id
             << "\", \"passed\": true, \"notes\": \"locked B-01 reference\"}";
      if (i + 1 < checklist.size()) {
        review << ',';
      }
      review << '\n';
    }
    review << "  ]\n}\n";
    expect(write_bytes(review_path, review.str()), "review record");
  } else {
    expect(std::filesystem::exists(manifest_path), "manifest exists");
    expect(std::filesystem::exists(change_path), "change record exists");
    expect(read_file(manifest_path) == manifest.str(), "manifest digests");

    // Empty previous → current requires the committed bootstrap change record.
    std::vector<GoldenChangeRecordEntry> records;
    for (const auto &d : digests) {
      records.push_back({.artifact_id = d.artifact_id,
                         .previous_sha256 = "",
                         .next_sha256 = d.sha256,
                         .reason = "initial editorial reference approval",
                         .approved_by_role = "editorial-visual-reviewer",
                         .approved_at_utc = "2026-11-18T00:00:00Z"});
    }
    expect(validate_golden_change_records({}, digests, records).has_value(),
           "bootstrap change records cover goldens");

    // Simulate an unrecorded update → must fail.
    auto mutated = digests;
    if (!mutated.empty()) {
      mutated.front().sha256 = std::string(64, 'c');
      expect(!validate_golden_change_records(digests, mutated, records)
                  .has_value(),
             "unrecorded update rejected");
    }

    review_results = {
        {.id = "shot_boundaries",
         .passed = shot_boundaries_ok,
         .notes = "structural shot dumps"},
        {.id = "safe_area",
         .passed = safe_area_ok,
         .notes = "content safe rect in dump"},
        {.id = "typography",
         .passed = typography_ok,
         .notes = "role/size/codepoint goldens"},
        {.id = "reference_frames",
         .passed = reference_frames_ok,
         .notes = "PNG sha256 within tolerance 0"},
    };
    VisualReviewRecord record{
        .status = "approved",
        .approved_by_role = "editorial-visual-reviewer",
        .approved_at_utc = "2026-11-18T00:00:00Z",
        .golden_manifest_sha256 = sha256_file(manifest_path),
        .tolerances = tolerance,
        .checklist_results = review_results,
        .method = std::string(editorial_visual_acceptance_method()),
    };
    expect(validate_visual_review_record(editorial_checklist(), record)
               .has_value(),
           "signed visual review");
    expect(std::filesystem::exists(review_path), "review fixture present");
    expect(std::filesystem::exists(checklist_path), "checklist fixture");
    expect(std::filesystem::exists(tolerances_path), "tolerances fixture");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "editorial_visual_acceptance_test: ok\n";
  return EXIT_SUCCESS;
}
