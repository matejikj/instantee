#include <instantee/core_model/time.hpp>
#include <instantee/core_model/transform.hpp>
#include <instantee/render_core/paper_fold.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::render_core;
using instantee::core::FrameIndex;
using instantee::core::nearly_equal;

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

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: paper_fold_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path golden_dir =
      root / "contracts/fixtures/paper-fold/goldens";

  auto mesh = make_letter_fold_mesh(120.0, 80.0);
  expect(validate_paper_fold_mesh(mesh).has_value(), "mesh validates");
  auto clip = make_letter_fold_clip(24, 6);
  expect(validate_paper_fold_clip(clip, mesh).has_value(), "clip validates");

  // Flat start: right edge still at x=120, z≈0.
  {
    auto pose = evaluate_paper_fold_clip(mesh, clip, FrameIndex{0});
    expect(pose.has_value(), "flat pose");
    if (pose) {
      expect(nearly_equal(pose->vertices[2].x, 120.0, 1e-6), "flat right x");
      expect(nearly_equal(pose->vertices[2].z, 0.0, 1e-6), "flat right z");
      expect(pose->draw_order.size() == 2U, "two segments");
    }
  }

  // Mid fold: right edge swings toward −Z (camera forward).
  {
    auto pose = evaluate_paper_fold_clip(mesh, clip, FrameIndex{12});
    expect(pose.has_value(), "half pose");
    if (pose) {
      expect(pose->vertices[2].z < -1.0, "half fold lifts into -Z");
      expect(std::abs(pose->vertices[2].x - 60.0) < 5.0,
             "half fold near crease x");
      // Random-access: same frame again.
      auto again = evaluate_paper_fold_clip(mesh, clip, FrameIndex{12});
      expect(again.has_value(), "half reevaluate");
      if (again) {
        expect(nearly_equal(pose->vertices[2], again->vertices[2]),
               "random-access identical");
      }
    }
  }

  // Fully folded: right panel stacks onto left (x <= mid), z≈0.
  {
    auto pose = evaluate_paper_fold_clip(mesh, clip, FrameIndex{24});
    expect(pose.has_value(), "closed pose");
    if (pose) {
      expect(pose->vertices[2].x < 60.5, "folded right x over left");
      expect(nearly_equal(pose->vertices[2].z, 0.0, 1e-5), "folded flat z");
      // Hinged panel should sort above base when depths tie.
      expect(pose->draw_order.back().segment_id == "panel_right" ||
                 pose->draw_order.front().segment_id == "panel_left",
             "occlusion order defined");
      const auto &front = pose->draw_order.back();
      expect(front.occlusion_order >= pose->draw_order.front().occlusion_order,
             "higher occlusion toward front");
    }
  }

  // Unfold end returns to flat.
  {
    auto pose = evaluate_paper_fold_clip(mesh, clip, FrameIndex{54});
    expect(pose.has_value(), "open pose");
    if (pose) {
      expect(nearly_equal(pose->vertices[2].x, 120.0, 1e-5), "unfolded x");
      expect(nearly_equal(pose->vertices[2].z, 0.0, 1e-5), "unfolded z");
    }
  }

  // Fail-closed: unknown crease angle / shared vertex.
  {
    auto bad = evaluate_paper_fold(mesh, {{"ghost", 1.0}});
    expect(!bad.has_value(), "unknown crease rejected");
    PaperFoldMesh broken = mesh;
    broken.segments[1].vertex_indices.push_back(0);
    expect(!validate_paper_fold_mesh(broken).has_value(),
           "shared vertex rejected");
  }

  // Goldens at flat / half / closed.
  struct Case {
    const char *name;
    std::uint64_t frame;
  };
  const Case cases[] = {{"flat-f0", 0}, {"half-f12", 12}, {"closed-f24", 24}};
  for (const auto &c : cases) {
    auto pose = evaluate_paper_fold_clip(mesh, clip, FrameIndex{c.frame});
    expect(pose.has_value(), "golden pose");
    if (!pose) {
      continue;
    }
    const std::string body =
        format_paper_fold_golden(c.name, mesh, *pose, FrameIndex{c.frame});
    const auto path = golden_dir / (std::string(c.name) + ".txt");
    expect(write_if_missing(path, body), "write golden");
    expect(read_file(path) == body, "golden matches");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
