#include <instantee/direction/composition_constraints.hpp>
#include <instantee/direction/editorial_asym_layout.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

[[nodiscard]] std::string
golden_dump(const instantee::direction::EditorialAsymLayout &layout,
            std::uint32_t count) {
  using instantee::direction::editorial_focal_is_readable;
  std::ostringstream ss;
  ss << "count=" << count << " elements=" << layout.elements.size()
     << " primary_w=" << layout.primary_column_width_px
     << " secondary_w=" << layout.secondary_column_width_px
     << " overhang_headline_w=" << layout.elements.front().width_px << '\n';
  ss << "focal x=" << layout.focal.x_px << " y=" << layout.focal.y_px
     << " readable=" << (editorial_focal_is_readable(layout.focal) ? 1 : 0)
     << '\n';
  ss << "band x=" << layout.focal.band_x_px << " y=" << layout.focal.band_y_px
     << " w=" << layout.focal.band_width_px
     << " h=" << layout.focal.band_height_px << '\n';
  for (const auto &el : layout.elements) {
    ss << el.id << " x=" << el.x_px << " y=" << el.y_px << " w=" << el.width_px
       << " h=" << el.height_px << " imp=" << el.importance << '\n';
  }
  return ss.str();
}

} // namespace

int main(int argc, char **argv) {
  using namespace instantee::direction;

  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr
        << "usage: editorial_asym_layout_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};

  auto bible = load_style_bible_file(
      root / "contracts/fixtures/style-bible/positive/editorial.style.json");
  expect(bible.has_value(), "load editorial style");
  if (!bible) {
    return EXIT_FAILURE;
  }

  expect(!compose_editorial_asymmetrical(
             editorial_asym_request_from_style(*bible, 1)),
         "reject count 1");
  expect(!compose_editorial_asymmetrical(
             editorial_asym_request_from_style(*bible, 6)),
         "reject count 6");

  const std::uint32_t counts[] = {2, 3, 5};
  for (std::uint32_t count : counts) {
    auto request = editorial_asym_request_from_style(*bible, count);
    auto layout = compose_editorial_asymmetrical(request);
    expect(layout.has_value(), "compose layout");
    if (!layout) {
      continue;
    }
    expect(layout->elements.size() == count, "element count");
    expect(layout->primary_column_width_px > layout->secondary_column_width_px,
           "asymmetry primary>secondary");
    expect(layout->elements.front().id == "headline", "headline first");
    expect(layout->elements.front().width_px > layout->primary_column_width_px,
           "headline overhang");
    expect(editorial_focal_is_readable(layout->focal), "focal readable");
    expect(layout->focal.inside_readable_band, "focal flag");

    // Hierarchy: later stack elements sit below earlier ones in the primary
    // column (callout may sit beside the stack).
    if (count >= 3) {
      expect(layout->elements[2].y_px > layout->elements[0].y_px,
             "body below headline");
    }
    if (count >= 4) {
      expect(layout->elements[3].x_px == layout->secondary_column_x_px,
             "callout in secondary");
    }

    // Composition solver accepts the placed elements with soft overlap policy.
    std::vector<CompositionConstraint> constraints;
    for (const LayoutElement &el : layout->elements) {
      constraints.push_back(
          CompositionConstraint{.kind = CompositionConstraintKind::bound_inside,
                                .severity = ConstraintSeverity::hard,
                                .element_id = el.id});
      if (el.keep_visible) {
        constraints.push_back(CompositionConstraint{
            .kind = CompositionConstraintKind::keep_visible,
            .severity = ConstraintSeverity::hard,
            .element_id = el.id});
      }
      if (el.text.has_value()) {
        constraints.push_back(CompositionConstraint{
            .kind = CompositionConstraintKind::text_legibility,
            .severity = ConstraintSeverity::hard,
            .element_id = el.id,
            .min_text_height_px = 20.0});
      }
    }
    auto solved =
        solve_composition(layout->elements, constraints,
                          request.canvas_width_px, request.canvas_height_px);
    expect(solved.has_value(), "solver accepts template");

    const std::string golden = golden_dump(*layout, count);
    const auto golden_path = root /
                             "contracts/fixtures/editorial-asym/goldens" /
                             ("count-" + std::to_string(count) + ".txt");
    if (write_goldens) {
      std::filesystem::create_directories(golden_path.parent_path());
      std::ofstream out(golden_path, std::ios::binary | std::ios::trunc);
      out << golden;
      expect(static_cast<bool>(out), "write golden");
    } else {
      expect(write_if_missing(golden_path, golden), "seed golden");
      expect(read_file(golden_path) == golden, "golden match");
    }

    // Determinism.
    auto again = compose_editorial_asymmetrical(request);
    expect(again.has_value() && golden_dump(*again, count) == golden,
           "deterministic compose");
  }

  // Bad ratios fail closed.
  {
    auto bad = editorial_asym_request_from_style(*bible, 3);
    bad.primary_column_ratio = 0.4;
    expect(!compose_editorial_asymmetrical(bad), "reject ratio");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
