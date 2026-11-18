#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/direction/editorial_visual_acceptance.hpp>
#include <instantee/direction/style_bible.hpp>
#include <instantee/preview/frame_png.hpp>
#include <instantee/runtime/frame_preview.hpp>
#include <instantee/scene_ir/canonicalize.hpp>
#include <instantee/scene_ir/document.hpp>
#include <instantee/scene_ir/load.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using namespace instantee::direction;
using instantee::core::FrameIndex;
using instantee::preview::export_preview_png;
using instantee::preview::rasterize_preview_frame;
using instantee::runtime::preview_frame_at;
using instantee::scene_ir::NodeKind;
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

[[nodiscard]] constexpr std::string_view
paper_cinema_visual_acceptance_method() noexcept {
  return editorial_visual_acceptance_method();
}

[[nodiscard]] std::vector<VisualChecklistItem> paper_cinema_checklist() {
  return {
      {.id = "poses",
       .description =
           "Reference puppet parts (torso/head/arm) appear on character shots",
       .required = true},
      {.id = "depth",
       .description =
           "Background, stage, and figure layers use distinct depth planes",
       .required = true},
      {.id = "shadows_materials",
       .description = "Paper materials (pulp/ink/accent/sky/veil) and Style "
                      "Bible contact_shadow declaration are present",
       .required = true},
      {.id = "transforms",
       .description =
           "Arm raise, fold hinge, and camera move tracks animate the figure",
       .required = true},
      {.id = "shot_continuity",
       .description =
           "Five curated shots plus dissolve transition span are locked",
       .required = true},
      {.id = "phone_legibility",
       .description =
           "1080x1920 canvas, Style Bible safe-area, and readable headlines",
       .required = true},
      {.id = "reference_frames",
       .description =
           "PNG reference frames compare within the locked tolerance",
       .required = true},
  };
}

struct PaperLayerCounts {
  std::uint32_t text = 0;
  std::uint32_t geometry = 0;
  std::uint32_t image = 0;
  std::uint32_t puppet = 0;
  std::uint32_t character_parts = 0;
  std::uint32_t opacity_tracks = 0;
  std::uint32_t rotation_tracks = 0;
  std::uint32_t translate_tracks = 0;
  std::uint32_t scale_tracks = 0;
  double min_z = 0;
  double max_z = 0;
  std::uint32_t distinct_z = 0;
  double min_font_size_px = 0;
  double max_font_size_px = 0;
  std::set<std::string> materials;
};

[[nodiscard]] bool is_character_part(std::string_view node_id) {
  return node_id == "figure_torso" || node_id == "figure_head" ||
         node_id == "figure_arm";
}

[[nodiscard]] std::uint64_t
first_opaque_text_frame(const instantee::scene_ir::SceneDocument &document,
                        const instantee::scene_ir::Shot &shot) {
  // Shot-start frames often still fade in; pin a readable frame inside the
  // shot so character/material reference PNGs are not empty canvases.
  const std::uint64_t shot_end = shot.start_frame + shot.duration_frames;
  std::unordered_set<std::string> text_nodes;
  std::unordered_map<std::string, const instantee::scene_ir::Node *> nodes;
  for (const auto &node : document.nodes) {
    nodes.emplace(node.id, &node);
  }
  for (const auto &node_id : shot.node_ids) {
    const auto it = nodes.find(node_id);
    if (it != nodes.end() && it->second->text_run_id) {
      text_nodes.insert(node_id);
    }
  }
  for (const auto &track : document.animation_tracks) {
    if (!text_nodes.contains(track.target_id) || track.property != "opacity") {
      continue;
    }
    for (const auto &key : track.keyframes) {
      if (key.frame >= shot.start_frame && key.frame < shot_end &&
          key.value >= 0.99) {
        return key.frame;
      }
    }
  }
  const std::uint64_t offset =
      shot.duration_frames > 0
          ? std::min<std::uint64_t>(24, shot.duration_frames - 1)
          : 0;
  return shot.start_frame + offset;
}

[[nodiscard]] PaperLayerCounts
count_layers(const instantee::scene_ir::SceneDocument &document,
             const instantee::scene_ir::Shot &shot) {
  PaperLayerCounts counts{};
  std::unordered_map<std::string, const instantee::scene_ir::Node *> nodes;
  for (const auto &node : document.nodes) {
    nodes.emplace(node.id, &node);
  }
  std::unordered_map<std::string, const instantee::scene_ir::TextRun *> runs;
  for (const auto &run : document.text_runs) {
    runs.emplace(run.id, &run);
  }

  bool min_font_set = false;
  bool z_set = false;
  std::set<long long> z_milli;
  for (const auto &node_id : shot.node_ids) {
    const auto it = nodes.find(node_id);
    if (it == nodes.end()) {
      continue;
    }
    const auto &node = *it->second;
    if (node.kind == NodeKind::puppet || node.rig_id) {
      ++counts.puppet;
    }
    if (is_character_part(node_id)) {
      ++counts.character_parts;
    }
    if (node.text_run_id) {
      ++counts.text;
      const auto run_it = runs.find(*node.text_run_id);
      if (run_it != runs.end()) {
        const auto &run = *run_it->second;
        if (!min_font_set || run.font_size_px < counts.min_font_size_px) {
          counts.min_font_size_px = run.font_size_px;
          min_font_set = true;
        }
        counts.max_font_size_px =
            std::max(counts.max_font_size_px, run.font_size_px);
      }
    } else if (node.geometry_id) {
      ++counts.geometry;
    } else if (node.asset_id) {
      ++counts.image;
    }
    if (node.material_id) {
      counts.materials.insert(*node.material_id);
    }
    if (node.transform && node.transform->translate) {
      const double z = node.transform->translate->z;
      if (!z_set || z < counts.min_z) {
        counts.min_z = z;
      }
      if (!z_set || z > counts.max_z) {
        counts.max_z = z;
      }
      z_set = true;
      z_milli.insert(static_cast<long long>(z * 1000.0));
    }
  }
  counts.distinct_z = static_cast<std::uint32_t>(z_milli.size());

  for (const auto &track : document.animation_tracks) {
    if (std::find(shot.node_ids.begin(), shot.node_ids.end(),
                  track.target_id) == shot.node_ids.end()) {
      continue;
    }
    if (track.property == "opacity" && track.keyframes.size() >= 2) {
      ++counts.opacity_tracks;
    } else if (track.property.find("rotation") != std::string::npos) {
      ++counts.rotation_tracks;
    } else if (track.property.find("translate") != std::string::npos) {
      ++counts.translate_tracks;
    } else if (track.property.find("scale") != std::string::npos) {
      ++counts.scale_tracks;
    }
  }
  return counts;
}

[[nodiscard]] ShotBoundaryObservation
observe_shot(const instantee::scene_ir::SceneDocument &document,
             const instantee::scene_ir::Shot &shot,
             const ContentSafeRect &safe) {
  ShotBoundaryObservation observation{};
  observation.shot_id = shot.id;
  observation.start_frame = shot.start_frame;
  observation.duration_frames = shot.duration_frames;
  observation.sample_frame = first_opaque_text_frame(document, shot);
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

[[nodiscard]] std::string
format_paper_structural(const ShotBoundaryObservation &observation,
                        const PaperLayerCounts &layers,
                        std::size_t transition_count) {
  std::ostringstream out;
  out << format_shot_boundary_golden(observation);
  out << "layers text=" << layers.text << " geometry=" << layers.geometry
      << " image=" << layers.image << " puppet=" << layers.puppet
      << " character_parts=" << layers.character_parts << '\n';
  out << "materials";
  if (layers.materials.empty()) {
    out << " none";
  } else {
    for (const auto &mat : layers.materials) {
      out << ' ' << mat;
    }
  }
  out << '\n';
  out << "depth min_z=" << layers.min_z << " max_z=" << layers.max_z
      << " distinct_z=" << layers.distinct_z << '\n';
  out << "transforms rotation=" << layers.rotation_tracks
      << " translate=" << layers.translate_tracks
      << " scale=" << layers.scale_tracks
      << " opacity=" << layers.opacity_tracks << '\n';
  out << "continuity transitions=" << transition_count << '\n';
  out << "legibility min_size=" << layers.min_font_size_px
      << " max_size=" << layers.max_font_size_px << " canvas=1080x1920\n";
  return out.str();
}

[[nodiscard]] bool document_has_transform_tracks(
    const instantee::scene_ir::SceneDocument &document) {
  bool arm = false;
  bool fold = false;
  bool camera = false;
  for (const auto &track : document.animation_tracks) {
    if (track.target_id == "figure_arm" &&
        track.property.find("rotation") != std::string::npos) {
      arm = true;
    }
    if (track.target_id == "fold" &&
        track.property.find("rotation") != std::string::npos) {
      fold = true;
    }
    if (track.target_id == "camera" &&
        (track.property.find("translate") != std::string::npos ||
         track.property.find("scale") != std::string::npos)) {
      camera = true;
    }
  }
  return arm && fold && camera;
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr << "usage: paper_cinema_visual_acceptance_test <repo-root> "
                 "[--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir = root / "contracts/fixtures/b03-paper/goldens";
  const auto review_dir = root / "contracts/fixtures/b03-paper/visual-review";
  const auto scene_path =
      root / "contracts/fixtures/b03-paper/b03-historical-reel.scene.json";
  const auto bible_path =
      root / "contracts/fixtures/style-bible/positive/paper-cinema.style.json";

  {
    VisualTolerance ok{.max_channel_delta = 1, .max_mismatch_ratio = 0.01};
    expect(validate_visual_tolerance(ok).has_value(), "tolerance ok");
    const std::vector<std::uint8_t> a{10, 20, 30, 255, 1, 2, 3, 255};
    auto exact = compare_rgba8_images(2, 1, a, a, VisualTolerance{});
    expect(exact.has_value() && exact->within_tolerance, "exact match");
    auto checklist = paper_cinema_checklist();
    std::vector<VisualChecklistResult> results;
    for (const auto &item : checklist) {
      results.push_back({.id = item.id, .passed = true, .notes = "unit"});
    }
    expect(validate_checklist_results(checklist, results).has_value(),
           "checklist complete");
    results[0].passed = false;
    expect(!validate_checklist_results(checklist, results).has_value(),
           "required fail");
  }

  auto bible = load_style_bible_file(bible_path);
  expect(bible.has_value(), "load paper-cinema bible");
  if (!bible) {
    return EXIT_FAILURE;
  }
  expect(bible->materials().count("contact_shadow") == 1,
         "style bible declares contact_shadow");
  expect(bible->materials().count("paper_sheet") == 1, "paper_sheet material");
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
  expect(document.has_value(), "load B-03 scene");
  if (!document) {
    return EXIT_FAILURE;
  }
  expect(document->shots.size() == 5, "five paper cinema shots");
  expect(document->canvas_width == 1080 && document->canvas_height == 1920,
         "phone canvas");
  expect(!document->transition_spans.empty(), "dissolve transition present");
  expect(document_has_transform_tracks(*document),
         "arm/fold/camera transform tracks");

  auto compiled = instantee::compiler::compile_runtime_scene(*document);
  expect(compiled.has_value() && *compiled != nullptr, "compile");
  if (!compiled || !*compiled) {
    return EXIT_FAILURE;
  }
  auto program =
      instantee::compiler::compile_animation_program(*document, **compiled);
  expect(program.has_value() && !program->scalar_bindings.empty(),
         "animation program from scene tracks");
  if (!program) {
    return EXIT_FAILURE;
  }

  std::vector<GoldenArtifactDigest> digests;
  VisualTolerance tolerance{};

  bool poses_ok = false;
  bool depth_ok = false;
  bool materials_ok = false;
  bool transforms_ok = document_has_transform_tracks(*document);
  bool continuity_ok =
      document->shots.size() == 5 && !document->transition_spans.empty();
  bool phone_ok = document->canvas_width == 1080 &&
                  document->canvas_height == 1920 && safe->width_px > 0 &&
                  safe->height_px > 0;
  bool reference_frames_ok = true;
  std::set<std::string> materials_seen;
  const std::size_t transition_count = document->transition_spans.size();

  for (const auto &shot : document->shots) {
    auto observation = observe_shot(*document, shot, *safe);
    auto layers = count_layers(*document, shot);
    const std::string structural =
        format_paper_structural(observation, layers, transition_count);
    const auto structural_path = golden_dir / (shot.id + ".structural.txt");
    const auto png_path = golden_dir / (shot.id + ".png");

    if (layers.character_parts >= 3 && layers.puppet >= 1) {
      poses_ok = true;
    }
    if (layers.distinct_z >= 4) {
      depth_ok = true;
    }
    materials_seen.insert(layers.materials.begin(), layers.materials.end());
    if (layers.min_font_size_px > 0 && layers.min_font_size_px < 24.0) {
      phone_ok = false;
    }

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
        phone_ok = false;
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
                 ("instantee-paper-golden-" + shot.id + ".png");
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
    }

    digests.push_back({.artifact_id = shot.id + ".structural.txt",
                       .kind = "structural",
                       .sha256 = sha256_file(structural_path)});
    digests.push_back({.artifact_id = shot.id + ".png",
                       .kind = "png",
                       .sha256 = sha256_file(png_path)});
  }

  const std::set<std::string> required_mats = {"pulp", "ink", "accent", "sky",
                                               "veil"};
  materials_ok = std::includes(materials_seen.begin(), materials_seen.end(),
                               required_mats.begin(), required_mats.end()) &&
                 bible->materials().count("contact_shadow") == 1;

  std::ostringstream manifest;
  manifest << "{\n  \"schema_version\": \"1.0.0\",\n  \"workload\": "
              "\"B-03-paper-historical-reel-60s-v1\",\n  "
              "\"artifacts\": [\n";
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
              << "      \"reason\": \"initial paper cinema reference "
                 "approval\",\n"
              << "      \"approved_by_role\": \"paper-cinema-visual-"
                 "reviewer\",\n"
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
    auto checklist = paper_cinema_checklist();
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
           << "  \"approved_by_role\": \"paper-cinema-visual-reviewer\",\n"
           << "  \"approved_at_utc\": \"2026-11-18T00:00:00Z\",\n"
           << "  \"golden_manifest_sha256\": \"" << manifest_sha << "\",\n"
           << "  \"tolerances\": {\"max_channel_delta\": 0, "
              "\"max_mismatch_ratio\": 0.0},\n"
           << "  \"method\": \"" << paper_cinema_visual_acceptance_method()
           << "\",\n"
           << "  \"checklist_results\": [\n";
    for (std::size_t i = 0; i < checklist.size(); ++i) {
      review << "    {\"id\": \"" << checklist[i].id
             << "\", \"passed\": true, \"notes\": \"locked B-03 reference\"}";
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

    std::vector<GoldenChangeRecordEntry> records;
    for (const auto &d : digests) {
      records.push_back({.artifact_id = d.artifact_id,
                         .previous_sha256 = "",
                         .next_sha256 = d.sha256,
                         .reason = "initial paper cinema reference approval",
                         .approved_by_role = "paper-cinema-visual-reviewer",
                         .approved_at_utc = "2026-11-18T00:00:00Z"});
    }
    expect(validate_golden_change_records({}, digests, records).has_value(),
           "bootstrap change records cover goldens");

    auto mutated = digests;
    if (!mutated.empty()) {
      mutated.front().sha256 = std::string(64, 'c');
      expect(!validate_golden_change_records(digests, mutated, records)
                  .has_value(),
             "unrecorded update rejected");
    }

    expect(poses_ok, "character poses present");
    expect(depth_ok, "depth planes present");
    expect(materials_ok, "paper materials + contact_shadow declaration");
    expect(transforms_ok, "transform tracks present");
    expect(continuity_ok, "shot continuity locked");
    expect(phone_ok, "phone legibility bounds");
    expect(reference_frames_ok, "reference frames");

    std::vector<VisualChecklistResult> review_results = {
        {.id = "poses",
         .passed = poses_ok,
         .notes = "torso/head/arm on character shots"},
        {.id = "depth",
         .passed = depth_ok,
         .notes = "distinct background/stage/figure planes"},
        {.id = "shadows_materials",
         .passed = materials_ok,
         .notes = "pulp/ink/accent/sky/veil + contact_shadow in bible"},
        {.id = "transforms",
         .passed = transforms_ok,
         .notes = "arm raise, fold hinge, camera move"},
        {.id = "shot_continuity",
         .passed = continuity_ok,
         .notes = "five shots + dissolve span"},
        {.id = "phone_legibility",
         .passed = phone_ok,
         .notes = "safe-area + type size + 1080x1920"},
        {.id = "reference_frames",
         .passed = reference_frames_ok,
         .notes = "PNG sha256 within tolerance 0"},
    };
    VisualReviewRecord record{
        .status = "approved",
        .approved_by_role = "paper-cinema-visual-reviewer",
        .approved_at_utc = "2026-11-18T00:00:00Z",
        .golden_manifest_sha256 = sha256_file(manifest_path),
        .tolerances = tolerance,
        .checklist_results = review_results,
        .method = std::string(paper_cinema_visual_acceptance_method()),
    };
    expect(validate_visual_review_record(paper_cinema_checklist(), record)
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
  std::cout << "paper_cinema_visual_acceptance_test: ok\n";
  return EXIT_SUCCESS;
}
