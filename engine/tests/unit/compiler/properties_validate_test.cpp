#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/compiler/validate_properties.hpp>
#include <instantee/core_model/properties.hpp>
#include <instantee/scene_ir/document.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

instantee::scene_ir::SceneDocument base_document() {
  instantee::scene_ir::SceneDocument document;
  document.schema_version = "1.0.0";
  document.compiler_version = "test";
  document.canvas_width = 1080;
  document.canvas_height = 1920;
  document.duration_frames = instantee::core::FrameIndex{30};
  instantee::scene_ir::Node node;
  node.id = "headline";
  node.kind = instantee::scene_ir::NodeKind::text;
  document.nodes.push_back(std::move(node));
  return document;
}

} // namespace

int main() {
  using namespace instantee::compiler;
  using namespace instantee::core;
  using namespace instantee::scene_ir;

  auto registry = make_builtin_property_registry();

  {
    auto document = base_document();
    AnimationTrack track;
    track.id = "t0";
    track.target_id = "headline";
    track.property = "opacity";
    track.keyframes.push_back(Keyframe{0, 0.0});
    track.keyframes.push_back(Keyframe{10, 1.0});
    document.animation_tracks.push_back(std::move(track));
    const auto ok = validate_animation_properties(document, registry);
    expect(ok.has_value(), "supported node property compiles");
  }

  {
    auto document = base_document();
    AnimationTrack track;
    track.id = "t1";
    track.target_id = "headline";
    track.property = "not_registered";
    track.keyframes.push_back(Keyframe{0, 0.0});
    document.animation_tracks.push_back(std::move(track));
    const auto bad = validate_animation_properties(document, registry);
    expect(!bad.has_value(), "unknown property fails at compile");
    if (!bad) {
      expect(bad.error().code == "unknown_property", "unknown_property code");
      expect(bad.error().phase == "compile", "compile phase");
    }
  }

  {
    // color is registered for material, not node → compile failure.
    auto document = base_document();
    AnimationTrack track;
    track.id = "t2";
    track.target_id = "headline";
    track.property = "color";
    track.keyframes.push_back(Keyframe{0, 0.0});
    document.animation_tracks.push_back(std::move(track));
    const auto bad = validate_animation_properties(document, registry);
    expect(!bad.has_value(), "material property on node fails at compile");
    if (!bad) {
      expect(bad.error().code == "unsupported_target_property",
             "unsupported_target_property code");
    }
  }

  {
    auto document = base_document();
    AnimationTrack track;
    track.id = "t3";
    track.target_id = "headline";
    track.property = "color";
    track.keyframes.push_back(Keyframe{0, 0.0});
    document.animation_tracks.push_back(std::move(track));
    const auto scene = compile_runtime_scene(document);
    expect(!scene.has_value(),
           "compile_runtime_scene rejects unsupported property");
    if (!scene) {
      expect(scene.error().code == "unsupported_target_property",
             "runtime compile reports unsupported_target_property");
      expect(scene.error().phase == "compile", "failure phase is compile");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
