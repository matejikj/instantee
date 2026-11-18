#include <instantee/render_core/paper_prop_blueprint.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

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

[[nodiscard]] bool same_topology(const ResolvedPaperProp &a,
                                 const ResolvedPaperProp &b) {
  if (a.blueprint_id != b.blueprint_id || a.kind != b.kind ||
      a.parts.size() != b.parts.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.parts.size(); ++i) {
    const auto &pa = a.parts[i];
    const auto &pb = b.parts[i];
    if (pa.id != pb.id || pa.shape != pb.shape ||
        pa.palette_slot != pb.palette_slot ||
        pa.material_slot != pb.material_slot ||
        pa.layer_order != pb.layer_order ||
        pa.depth_offset != pb.depth_offset) {
      return false;
    }
    if (pa.bounds.x != pb.bounds.x || pa.bounds.y != pb.bounds.y ||
        pa.bounds.width != pb.bounds.width ||
        pa.bounds.height != pb.bounds.height) {
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path root = (argc > 1)
                                         ? std::filesystem::path{argv[1]}
                                         : std::filesystem::current_path();
  const auto golden_dir =
      root / "contracts/fixtures/paper-prop-blueprint/goldens";

  {
    expect(parse_paper_prop_kind("building").value() == PaperPropKind::building,
           "parse building");
    expect(!parse_paper_prop_kind("spaceship").has_value(),
           "reject unknown kind");
  }

  {
    auto bad = make_instrument_prop_blueprint();
    bad.parts[0].id = bad.parts[1].id;
    expect(!validate_paper_prop_blueprint(bad).has_value(),
           "reject duplicate part ids");
    bad = make_instrument_prop_blueprint();
    bad.parts[0].local_bounds.x = -1;
    expect(!validate_paper_prop_blueprint(bad).has_value(),
           "reject part outside frame");
  }

  auto palette = make_default_paper_prop_palette();
  auto materials = make_default_paper_prop_materials();

  {
    // Missing binding fails closed without inventing colors/materials.
    auto bp = make_cloud_prop_blueprint();
    PaperPropPalette empty;
    expect(!instantiate_paper_prop(bp, empty, materials).has_value(),
           "missing palette rejected");
  }

  const std::array factories = {
      +[] { return make_instrument_prop_blueprint(); },
      +[] { return make_building_prop_blueprint(); },
      +[] { return make_cloud_prop_blueprint(); },
  };
  const std::array labels = {"instrument", "building", "cloud"};

  for (std::size_t i = 0; i < factories.size(); ++i) {
    const PaperPropBlueprint blueprint = factories[i]();
    expect(validate_paper_prop_blueprint(blueprint).has_value(),
           "blueprint validates");

    auto resolved = instantiate_paper_prop(blueprint, palette, materials);
    expect(resolved.has_value(), "instantiate default bindings");
    if (!resolved) {
      continue;
    }
    expect(resolved->parts.size() == blueprint.parts.size(), "part count");

    // Reuse with a different palette: topology identical, colors change.
    PaperPropPalette alt = palette;
    alt.slots["body"] = Rgba8{120, 160, 90, 255};
    alt.slots["accent"] = Rgba8{40, 90, 160, 255};
    alt.slots["sky"] = Rgba8{250, 240, 210, 255};
    auto reused = instantiate_paper_prop(blueprint, alt, materials);
    expect(reused.has_value(), "instantiate alternate palette");
    if (reused) {
      expect(same_topology(*resolved, *reused),
             "palette swap preserves topology");
      bool color_changed = false;
      for (std::size_t p = 0; p < resolved->parts.size(); ++p) {
        const Rgba8 &a = resolved->parts[p].color;
        const Rgba8 &b = reused->parts[p].color;
        if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) {
          color_changed = true;
          break;
        }
      }
      expect(color_changed, "palette swap changes at least one color");
    }

    // Reuse with different materials: topology identical, material params
    // change.
    PaperPropMaterialSet alt_mats = materials;
    for (const char *slot : {"stock", "ink", "soft"}) {
      PaperMaterialParams tuned = alt_mats.slots[slot];
      tuned.fiber = 0.9f;
      tuned.seed = 99;
      alt_mats.slots[slot] = tuned;
    }
    auto rematted = instantiate_paper_prop(blueprint, palette, alt_mats);
    expect(rematted.has_value(), "instantiate alternate materials");
    if (rematted) {
      expect(same_topology(*resolved, *rematted),
             "material swap preserves topology");
      bool material_changed = false;
      for (std::size_t p = 0; p < resolved->parts.size(); ++p) {
        if (resolved->parts[p].material.seed !=
                rematted->parts[p].material.seed ||
            resolved->parts[p].material.fiber !=
                rematted->parts[p].material.fiber) {
          material_changed = true;
          break;
        }
      }
      expect(material_changed, "material swap changes bound params");
    }

    const std::string dump =
        format_paper_prop_blueprint_snapshot(labels[i], blueprint, *resolved);
    const auto golden_path = golden_dir / (std::string(labels[i]) + ".txt");
    expect(write_if_missing(golden_path, dump), "write golden if missing");
    const std::string expected = read_file(golden_path);
    expect(!expected.empty(), "golden readable");
    expect(dump == expected, "blueprint snapshot match");
    if (dump != expected) {
      std::cerr << "--- expected (" << labels[i] << ") ---\n"
                << expected << "--- actual ---\n"
                << dump;
    }
  }

  {
    // Scale reuses the same blueprint topology with larger bounds.
    auto bp = make_building_prop_blueprint(100, 140, 2.0f);
    auto scaled = instantiate_paper_prop(bp, palette, materials);
    expect(scaled.has_value(), "scaled instantiate");
    if (scaled) {
      expect(scaled->frame_width == 200.0f && scaled->frame_height == 280.0f,
             "scaled frame");
      expect(scaled->parts.front().bounds.width ==
                 bp.parts.front().local_bounds.width * 2.0f,
             "scaled part width");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
