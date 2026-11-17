#include <instantee/compiler/compile.hpp>
#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/load.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

} // namespace

int main() {
  const std::filesystem::path root = INSTANTEE_SOURCE_DIR;
  const auto text = read_file(
      root / "contracts/fixtures/authoring/positive/minimal.authoring.json");
  instantee::compiler::CompileOptions options;
  const auto *bytes = reinterpret_cast<const std::byte *>(text.data());
  auto first = instantee::compiler::compile_authoring_json(
      std::span<const std::byte>(bytes, text.size()), options);
  auto second = instantee::compiler::compile_authoring_json(
      std::span<const std::byte>(bytes, text.size()), options);
  expect(first.has_value() && second.has_value(), "CDS compile twice");
  if (first && second) {
    expect(!first->animation_tracks.empty(), "reveal emits tracks");
    expect(first->animation_tracks.front().property == "opacity",
           "opacity property");
    expect(first->animation_tracks.front().target_id == "headline",
           "target headline");
    expect(first->animation_tracks.front().keyframes.size() >= 2U,
           "keyframes present");
    auto a = instantee::compiler::scene_document_to_json(*first);
    auto b = instantee::compiler::scene_document_to_json(*second);
    expect(a && b && *a == *b, "deterministic CDS IR snapshot");
    expect(a->find("\"intent\"") == std::string::npos,
           "no unresolved intent in IR JSON");
  }

  // Unsupported direction must fail closed — never compile as empty tracks.
  instantee::scene_ir::AuthoringDocument doc;
  doc.schema_version = "1.0.0";
  doc.film_id = "bad-intent";
  doc.canvas_width = 1080;
  doc.canvas_height = 1920;
  doc.frame_rate = instantee::core::FrameRate::media_30_1();
  doc.seed = 1;
  doc.style = "editorial@1";
  instantee::scene_ir::AuthoringShot shot;
  shot.id = "s0";
  shot.duration_frames = 30;
  instantee::scene_ir::AuthoringIntent intent;
  intent.type = "morph_explode";
  intent.intensity = 1.0;
  intent.target_ids = {"n0"};
  shot.intent = intent;
  instantee::scene_ir::AuthoringNode node;
  node.id = "n0";
  node.type = "text";
  node.text = "x";
  shot.nodes.push_back(node);
  doc.shots.push_back(shot);

  auto rejected = instantee::compiler::compile_authoring(doc, options);
  expect(!rejected.has_value(), "reject unsupported intent");
  if (!rejected) {
    expect(rejected.error().code == "unsupported_intent",
           "unsupported_intent code");
  }

  // Schema-listed but unimplemented CDS kind (resolve) is also rejected.
  intent.type = "resolve";
  shot.intent = intent;
  doc.shots.clear();
  doc.shots.push_back(shot);
  auto resolve_rejected = instantee::compiler::compile_authoring(doc, options);
  expect(!resolve_rejected.has_value(), "reject unimplemented resolve");
  if (!resolve_rejected) {
    expect(resolve_rejected.error().code == "unsupported_intent",
           "resolve unsupported code");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
