#include <instantee/render_core/paper_notes.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
                                    std::string_view contents) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << contents;
  return static_cast<bool>(out);
}

[[nodiscard]] bool check_golden(const std::filesystem::path &path,
                                std::string_view actual, bool write_goldens) {
  if (write_goldens) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << actual;
    return true;
  }
  if (!write_if_missing(path, actual)) {
    return false;
  }
  return read_file(path) == actual;
}

[[nodiscard]] bool only_shared_kinds(const std::vector<DrawOp> &ops) {
  for (const auto &op : ops) {
    switch (op.kind) {
    case DrawOpKind::clear:
    case DrawOpKind::fill_rect:
    case DrawOpKind::fill_ellipse:
    case DrawOpKind::fill_rounded_rect:
    case DrawOpKind::stroke_line:
      break;
    default:
      return false;
    }
  }
  return !ops.empty();
}

[[nodiscard]] PaperNoteScene make_catalog_scene() {
  PaperNoteScene scene;
  scene.width_px = 160;
  scene.height_px = 100;
  scene.clear_color = Rgba8{255, 255, 255, 255};

  StickyNoteSpec sticky;
  sticky.id = "sticky-1";
  sticky.bounds = Rect2f{.x = 8, .y = 12, .width = 44, .height = 40};
  sticky.paper.base_color = Rgba8{255, 240, 140, 255};
  sticky.paper.wash_color = Rgba8{255, 250, 190, 255};
  sticky.motion.attach = 1.0f;
  sticky.motion.uncover = 1.0f;

  IndexCardSpec card;
  card.id = "index-1";
  card.bounds = Rect2f{.x = 60, .y = 10, .width = 42, .height = 50};
  card.ruled_lines = true;
  card.rule_spacing_px = 8.0f;

  PhotoMountSpec mount;
  mount.id = "mount-1";
  mount.mount_bounds = Rect2f{.x = 110, .y = 8, .width = 42, .height = 54};
  mount.photo_frame = Rect2f{.x = 116, .y = 14, .width = 30, .height = 36};
  mount.photo_color = Rgba8{120, 140, 160, 255};

  scene.notes.push_back(sticky);
  scene.notes.push_back(card);
  scene.notes.push_back(mount);
  return scene;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: paper_notes_test <source_root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  bool write_goldens = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--write-goldens") {
      write_goldens = true;
    }
  }
  const auto golden_dir = root / "contracts/fixtures/paper-notes/goldens";

  // Shared shader features: paper-sheet + existing shadow features.
  {
    auto registry = make_paper_note_shader_feature_registry();
    expect(registry.contains(std::string{kPaperSheetFeature}), "paper-sheet");
    expect(registry.contains(std::string{kDropShadowFeature}), "drop_shadow");
    expect(registry.contains(std::string{kSoftShadowFeature}), "soft_shadow");
    expect(!registry.contains("paper-cinema"), "no style package key");
    expect(!registry.contains("scientific-notebook"),
           "no notebook package key");

    MaterialDesc material{
        .id = "sticky_note",
        .version = 1,
        .feature_names = {std::string{kPaperSheetFeature},
                          std::string{kDropShadowFeature}},
    };
    auto active = plan_paper_note_passes(
        registry, material,
        std::vector<std::string>{std::string{kPaperSheetFeature},
                                 std::string{kDropShadowFeature}});
    expect(active.has_value(), "plan active features");
    if (active) {
      expect(active->paper_sheet_active, "paper-sheet pass planned");
      expect(active->shadow_active, "shadow pass planned");
    }
    auto inactive =
        plan_paper_note_passes(registry, material, std::vector<std::string>{});
    expect(inactive.has_value(), "inactive plan");
    if (inactive) {
      expect(!inactive->paper_sheet_active && !inactive->shadow_active,
             "inactive features add no passes");
      expect(inactive->pass_keys.empty(), "empty pass list when inactive");
    }
  }

  // Catalog golden: sticky + index + photo mount on shared DrawOps.
  {
    auto scene = make_catalog_scene();
    auto pass = compose_paper_note_pass(scene);
    expect(pass.has_value(), "compose catalog");
    if (pass) {
      expect(only_shared_kinds(pass->ops), "shared DrawOp kinds only");
      auto img = rasterize_paper_note_scene(scene);
      expect(img.has_value(), "raster catalog");
      if (img) {
        const std::string dump =
            format_paper_note_golden("catalog", scene, *pass, *img);
        expect(check_golden(golden_dir / "catalog.txt", dump, write_goldens),
               "catalog golden");
      }
    }
  }

  // Stick / uncover motion changes presentation without new renderer path.
  {
    auto scene = make_catalog_scene();
    auto &sticky = std::get<StickyNoteSpec>(scene.notes[0]);
    sticky.motion.attach = 0.4f;
    sticky.motion.uncover = 0.55f;
    sticky.motion.peel_lift_y_px = -12.0f;

    auto pass = compose_paper_note_pass(scene);
    expect(pass.has_value(), "compose motion");
    if (pass) {
      expect(only_shared_kinds(pass->ops), "motion still shared ops");
      auto img = rasterize_paper_note_scene(scene);
      expect(img.has_value(), "raster motion");
      if (img) {
        const std::string dump =
            format_paper_note_golden("stick-uncover", scene, *pass, *img);
        expect(
            check_golden(golden_dir / "stick-uncover.txt", dump, write_goldens),
            "stick-uncover golden");
      }
    }

    // Fully uncovered attached card differs from peeled partial uncover.
    auto attached = make_catalog_scene();
    auto peeled = make_catalog_scene();
    std::get<StickyNoteSpec>(peeled.notes[0]).motion.attach = 0.0f;
    auto a = rasterize_paper_note_scene(attached);
    auto b = rasterize_paper_note_scene(peeled);
    expect(a && b && a->rgba != b->rgba, "attach changes pixels");
  }

  // Validation.
  {
    expect(!validate_paper_sheet_params(PaperSheetParams{.fiber = 1.5f})
                .has_value(),
           "reject fiber out of range");
    PaperNoteScene bad = make_catalog_scene();
    bad.width_px = 0;
    expect(!compose_paper_note_pass(bad).has_value(), "reject zero size");

    PhotoMountSpec mount;
    mount.id = "bad";
    mount.mount_bounds = Rect2f{.x = 10, .y = 10, .width = 40, .height = 40};
    mount.photo_frame = Rect2f{.x = 5, .y = 5, .width = 20, .height = 20};
    PaperNoteScene outside;
    outside.width_px = 80;
    outside.height_px = 80;
    outside.notes.push_back(mount);
    expect(!compose_paper_note_pass(outside).has_value(),
           "reject photo outside mat");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "paper_notes_test ok\n";
  return EXIT_SUCCESS;
}
