#include <instantee/direction/paper_cinematic_templates.hpp>
#include <instantee/render_core/paper_cinematic_lighting.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "direction tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::direction;
using namespace instantee::render_core;

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

[[nodiscard]] bool write_if_missing(const std::filesystem::path &path,
                                    std::string_view body) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] PaperCinematicLightingRecipe
recipe_for(PaperCinematicShotKind kind) {
  auto parsed = parse_paper_cinematic_lighting_recipe(
      paper_cinematic_lighting_recipe_id_for(kind));
  if (!parsed) {
    std::cerr << "FAIL: recipe parse " << parsed.error().code << '\n';
    std::exit(EXIT_FAILURE);
  }
  return *parsed;
}

[[nodiscard]] bool line_has_style_name_branch(std::string_view line) {
  const auto trimmed_start = line.find_first_not_of(" \t");
  if (trimmed_start == std::string_view::npos) {
    return false;
  }
  line.remove_prefix(trimmed_start);
  if (line.starts_with("//") || line.starts_with("/*") ||
      line.starts_with("*")) {
    return false;
  }
  const bool conditional = line.starts_with("if") ||
                           line.starts_with("else if") ||
                           line.starts_with("case");
  if (!conditional) {
    return false;
  }
  return line.find("\"paper-cinema\"") != std::string_view::npos ||
         line.find("\"editorial\"") != std::string_view::npos ||
         line.find("\"scientific-notebook\"") != std::string_view::npos;
}

// Automatic check: core-model + render-core must not branch on Style Bible
// package names. Comment lines documenting the rule are allowed.
[[nodiscard]] bool
audit_no_style_name_branches(const std::filesystem::path &source_root,
                             std::string &report) {
  const std::filesystem::path roots[] = {
      source_root / "engine/src/render-core",
      source_root / "engine/src/core-model",
  };
  bool ok = true;
  for (const auto &root : roots) {
    if (!std::filesystem::exists(root)) {
      continue;
    }
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto ext = entry.path().extension().string();
      if (ext != ".hpp" && ext != ".cpp" && ext != ".h" && ext != ".mm") {
        continue;
      }
      const auto body = read_file(entry.path());
      std::size_t line_no = 1;
      std::size_t cursor = 0;
      while (cursor <= body.size()) {
        const auto nl = body.find('\n', cursor);
        const auto end = nl == std::string::npos ? body.size() : nl;
        const std::string_view line(body.data() + cursor, end - cursor);
        if (line_has_style_name_branch(line)) {
          ok = false;
          report += entry.path().lexically_relative(source_root).string();
          report += ':';
          report += std::to_string(line_no);
          report += ": style-name branch\n";
        }
        if (nl == std::string::npos) {
          break;
        }
        cursor = nl + 1;
        ++line_no;
      }
    }
  }
  return ok;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: paper_cinematic_templates_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path fixture_dir =
      root / "contracts/fixtures/paper-cinematic-sequence";

  PaperCinematicSequenceRequest req;
  req.film_id = "paper-sequence-v1";
  req.camera_node_id = "camera";
  req.shots = {
      {.kind = PaperCinematicShotKind::reveal_stage,
       .duration_frames = 90,
       .camera_intensity = 0.4},
      {.kind = PaperCinematicShotKind::object_closeup,
       .duration_frames = 75,
       .camera_intensity = 0.35},
      {.kind = PaperCinematicShotKind::puppet_dialogue,
       .duration_frames = 120,
       .camera_intensity = 0.25},
      {.kind = PaperCinematicShotKind::technical_explanation,
       .duration_frames = 100,
       .camera_intensity = 0.3},
      {.kind = PaperCinematicShotKind::atmospheric_dissolve,
       .duration_frames = 80,
       .camera_intensity = 0.45,
       .dissolve_overlap_frames = 12},
  };

  auto assembly = assemble_paper_cinematic_sequence(req);
  expect(assembly.has_value(), "assemble sequence");
  if (!assembly) {
    std::cerr << assembly.error().code << ": " << assembly.error().message
              << '\n';
    return EXIT_FAILURE;
  }
  expect(assembly->shots.size() == 5U, "five templates");
  expect(assembly->total_duration_frames == 465U, "packed duration");
  expect(assembly->transitions.spans.size() == 1U, "dissolve transition");
  if (!assembly->transitions.spans.empty()) {
    expect(assembly->transitions.spans[0].blend == "dissolve",
           "reveal lowers to dissolve");
  }

  auto registry = make_paper_cinematic_lighting_feature_registry();
  expect(!registry.contains("paper-cinema"), "registry has no style package");
  expect(!registry.contains("editorial"), "registry has no editorial package");

  std::ostringstream graphs_manifest;
  graphs_manifest << "paper_cinematic_graphs\n";
  for (const auto &shot : assembly->shots) {
    expect(shot.lighting_recipe_id ==
               paper_cinematic_lighting_recipe_id_for(shot.kind),
           "recipe id stable");
    expect(registry.contains(shot.lighting_recipe_id), "feature registered");

    PaperCinematicShotGraphRequest graph_req;
    graph_req.recipe = recipe_for(shot.kind);
    graph_req.width_px = 320;
    graph_req.height_px = 180;
    graph_req.subject = {.x = 110, .y = 40, .width = 100, .height = 100};
    graph_req.support = {.x = 40, .y = 20, .width = 240, .height = 140};
    graph_req.subject_depth = 4;
    graph_req.support_depth = 10;
    if (shot.kind == PaperCinematicShotKind::atmospheric_dissolve) {
      graph_req.dissolve_progress = 0.4f;
    }
    auto graph = build_paper_cinematic_shot_graph(graph_req);
    expect(graph.has_value(), "build shared graph");
    if (!graph) {
      continue;
    }
    auto compiled = compile_render_graph(*graph);
    expect(compiled.has_value(), "compile shared graph");
    if (!compiled) {
      continue;
    }
    expect(compiled->pass_order.size() == 2U, "shadow + main live");
    expect(!compiled->barriers.empty(), "shadow write→sample barrier");
    graphs_manifest << format_paper_cinematic_shot_graph_manifest(
        graph_req, *graph, *compiled);
  }

  {
    const std::string body =
        format_paper_cinematic_sequence_manifest(*assembly);
    const auto path = fixture_dir / "sequence.txt";
    expect(write_if_missing(path, body), "write sequence fixture");
    expect(read_file(path) == body, "sequence fixture matches");
  }
  {
    const std::string body = graphs_manifest.str();
    const auto path = fixture_dir / "graphs.txt";
    expect(write_if_missing(path, body), "write graphs fixture");
    expect(read_file(path) == body, "graphs fixture matches");
  }

  {
    auto bad = req;
    bad.shots[0].kind = PaperCinematicShotKind::object_closeup;
    expect(!assemble_paper_cinematic_sequence(bad).has_value(),
           "out-of-order rejected");
  }

  std::string audit_report;
  expect(audit_no_style_name_branches(root, audit_report),
         "no style-name branches in core/render-core");
  if (!audit_report.empty()) {
    std::cerr << audit_report;
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
