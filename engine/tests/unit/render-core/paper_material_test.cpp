#include <instantee/direction/style_bible.hpp>
#include <instantee/render_core/paper_material.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

using instantee::direction::load_style_bible_file;
using instantee::direction::resolve_style_bible_epoch;
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
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: paper_material_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];

  expect(std::string{kPaperMaterialModelVersion} == "1.0.0", "model version");
  const auto defaults = paper_material_defaults_v1();
  expect(defaults.fiber == 0.4f, "default fiber");
  expect(defaults.roughness == 0.35f, "default roughness");
  expect(defaults.color_wash == 0.12f, "default color_wash");
  expect(defaults.edge_thickness == 0.04f, "default edge_thickness");
  expect(defaults.shadow_bias == 0.06f, "default shadow_bias");
  expect(defaults.normal_variation == 0.15f, "default normal_variation");
  expect(defaults.seed == 1u, "default seed");
  expect(validate_paper_material_params(defaults).has_value(),
         "defaults validate");

  auto empty = parse_paper_material_params({});
  expect(empty.has_value(), "empty map uses defaults");
  if (empty) {
    expect(empty->fiber == defaults.fiber && empty->seed == defaults.seed,
           "defaults preserved");
  }

  auto full = parse_paper_material_params({
      {"fiber", 0.62},
      {"roughness", 0.48},
      {"color_wash", 0.2},
      {"edge_thickness", 0.08},
      {"shadow_bias", 0.1},
      {"normal_variation", 0.28},
      {"seed", 42},
  });
  expect(full.has_value(), "full overrides parse");
  if (full) {
    expect(full->fiber == 0.62f, "override fiber");
    expect(full->seed == 42u, "override seed");
  }

  auto bad_fiber = parse_paper_material_params({{"fiber", 1.5}});
  expect(!bad_fiber.has_value(), "reject fiber out of range");
  if (!bad_fiber) {
    expect(bad_fiber.error().code == "paper_material.fiber_range",
           "fiber_range code");
  }

  auto unknown = parse_paper_material_params({{"fold", 0.2}});
  expect(!unknown.has_value(), "reject unknown param");
  if (!unknown) {
    expect(unknown.error().code == "paper_material.unknown_param",
           "unknown_param code");
  }

  auto bad_seed = parse_paper_material_params({{"seed", -1.0}});
  expect(!bad_seed.has_value(), "reject negative seed");
  if (!bad_seed) {
    expect(bad_seed.error().code == "paper_material.seed_range",
           "seed_range code");
  }

  auto partial = parse_paper_material_params({{"fiber", 0.5}});
  expect(partial.has_value(), "partial override");
  if (partial) {
    expect(partial->fiber == 0.5f, "partial fiber");
    expect(partial->roughness == defaults.roughness, "other defaults remain");
  }

  const auto descs = paper_material_shader_param_descs();
  expect(descs.size() == 7, "seven shader param descs");
  expect(descs.front().name == kPaperParamFiber, "first param fiber");

  expect(full.has_value(), "full available for sheet mapping");
  if (full) {
    auto sheet = paper_material_to_sheet_params(*full);
    expect(sheet.fiber == full->fiber, "sheet fiber");
    expect(sheet.edge_thickness_px == full->edge_thickness * 32.0f,
           "edge maps to px");
    expect(validate_paper_sheet_params(sheet).has_value(),
           "mapped sheet validates");
  }

  const auto bible_path =
      root / "contracts/fixtures/style-bible/positive/paper-cinema.style.json";
  auto bible = load_style_bible_file(bible_path);
  expect(bible.has_value(), "load paper-cinema bible");
  if (bible) {
    const auto &mat = bible->materials().at("paper_sheet");
    expect(mat.shader_feature == "paper-sheet", "feature id");
    auto parsed = parse_paper_material_params(mat.parameters);
    expect(parsed.has_value(), "bible base params parse");
    if (parsed) {
      expect(parsed->fiber == defaults.fiber, "bible matches defaults fiber");
      expect(parsed->normal_variation == defaults.normal_variation,
             "bible has normal_variation");
    }
    std::uint32_t distinct_seeds = 0;
    std::uint32_t last_seed = 0;
    bool first = true;
    for (const std::string &epoch_id : bible->epoch_ids()) {
      auto resolved = resolve_style_bible_epoch(*bible, epoch_id);
      expect(resolved.has_value(), "resolve epoch for material parse");
      if (!resolved) {
        continue;
      }
      expect(resolved->materials().at("paper_sheet").shader_feature ==
                 "paper-sheet",
             "epoch keeps feature");
      auto epoch_params = parse_paper_material_params(
          resolved->materials().at("paper_sheet").parameters);
      expect(epoch_params.has_value(), "epoch paper params parse");
      if (epoch_params) {
        if (first || epoch_params->seed != last_seed) {
          ++distinct_seeds;
          last_seed = epoch_params->seed;
          first = false;
        }
      }
    }
    expect(distinct_seeds >= 2, "epochs vary material seed");
  }

  expect(std::filesystem::exists(
             root / "contracts/schemas/paper-material-params-v1.schema.json"),
         "schema present");
  expect(std::filesystem::exists(root / "contracts/fixtures/paper-material/"
                                        "positive/defaults-only.json"),
         "positive defaults fixture");
  expect(std::filesystem::exists(root / "contracts/fixtures/paper-material/"
                                        "negative/unknown-param.json"),
         "negative unknown fixture");
  expect(!read_file(root / "contracts/fixtures/paper-material/positive/"
                           "defaults-only.json")
              .empty(),
         "defaults fixture readable");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
