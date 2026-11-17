#include <instantee/preview/frame_png.hpp>
#include <instantee/runtime/compiled_scene.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

using namespace instantee::preview;
using namespace instantee::runtime;
using instantee::core::FrameIndex;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::unique_ptr<const CompiledScene> make_tiny_scene() {
  CompiledSceneDraft draft;
  draft.canvas_width = 32;
  draft.canvas_height = 32;
  draft.canvas_color = "rec709-sdr";
  draft.duration_frames = FrameIndex{1000};
  draft.seed = 7;
  draft.node_ids = {"box"};
  RuntimeNode node;
  node.kind = RuntimeNodeKind::geometry;
  node.layer_order = 1;
  node.visible = true;
  node.transform.translate_x = 4;
  node.transform.translate_y = 4;
  draft.nodes.push_back(node);
  auto scene = make_compiled_scene(std::move(draft));
  if (!scene) {
    return nullptr;
  }
  return std::move(*scene);
}

} // namespace

int main() {
  auto scene = make_tiny_scene();
  expect(scene != nullptr, "tiny scene");
  if (!scene) {
    return EXIT_FAILURE;
  }

  AnimationProgram program;
  const auto png_path =
      std::filesystem::temp_directory_path() / "instantee-preview-unit.png";
  std::error_code ec;
  std::filesystem::remove(png_path, ec);

  auto first = export_preview_png(*scene, program, FrameIndex{450}, png_path);
  expect(first.has_value(), "export at 450");
  if (first) {
    expect(first->evaluations == 1, "single evaluation");
    expect(first->state.frame == FrameIndex{450}, "frame index");
  }
  expect(std::filesystem::exists(png_path), "png exists");

  // Seek again at a different frame without depending on prior raster work.
  auto second = export_preview_png(*scene, program, FrameIndex{0}, png_path);
  expect(second.has_value() && second->evaluations == 1, "seek 0 single eval");

  // Roundtrip signature + non-empty file.
  if (std::filesystem::exists(png_path)) {
    std::ifstream input(png_path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
    expect(bytes.size() > 32 && bytes[0] == 137 && bytes[1] == 80, "png bytes");
    std::filesystem::remove(png_path, ec);
  }

  // Reject empty path write of mismatched buffer.
  expect(!write_rgba8_png(png_path, 2, 2, std::span<const std::uint8_t>{}),
         "reject size mismatch");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "frame_png_test ok\n";
  return EXIT_SUCCESS;
}
