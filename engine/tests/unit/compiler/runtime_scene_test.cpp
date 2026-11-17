#include <instantee/compiler/compile.hpp>
#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/runtime/render_job.hpp>
#include <instantee/scene_ir/authoring.hpp>

#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

instantee::scene_ir::AuthoringDocument sample_authoring() {
  using namespace instantee::scene_ir;
  AuthoringDocument document;
  document.schema_version = "1.0.0";
  document.film_id = "runtime-scene";
  document.canvas_width = 1080;
  document.canvas_height = 1920;
  document.seed = 11;
  document.style = "editorial@1";
  AuthoringShot shot;
  shot.id = "intro";
  shot.duration_frames = 30;
  AuthoringIntent intent;
  intent.type = "reveal";
  intent.intensity = 0.8;
  intent.importance = 0.9;
  intent.target_ids = {"headline"};
  shot.intent = intent;
  AuthoringNode group;
  group.id = "group_a";
  group.type = "group";
  AuthoringNode text;
  text.id = "headline";
  text.type = "text";
  text.text = "Hello";
  text.role = "headline";
  text.translate_x = 10;
  text.translate_y = 20;
  group.children.push_back(std::move(text));
  shot.nodes.push_back(std::move(group));
  document.shots.push_back(std::move(shot));
  return document;
}

} // namespace

int main() {
  using namespace instantee::compiler;
  using namespace instantee::runtime;

  static_assert(!std::is_copy_constructible_v<CompiledScene>,
                "CompiledScene must be move-only");
  static_assert(!std::is_copy_assignable_v<CompiledScene>,
                "CompiledScene must be move-only");

  const auto ir = compile_authoring(sample_authoring(), CompileOptions{});
  expect(ir.has_value(), "identity compile authoring");
  if (!ir) {
    return EXIT_FAILURE;
  }

  auto scene = compile_runtime_scene(*ir);
  expect(scene.has_value(), "lower Scene IR to CompiledScene");
  if (!scene) {
    std::cerr << scene.error().snapshot() << '\n';
    return EXIT_FAILURE;
  }

  expect((*scene)->node_ids().size() == (*scene)->nodes().size(),
         "ids align with nodes");
  expect((*scene)->find_node_index("headline").has_value(), "headline present");
  expect((*scene)->find_node_index("group_a").has_value(), "group present");

  const auto headline = *(*scene)->find_node_index("headline");
  const auto group = *(*scene)->find_node_index("group_a");
  expect((*scene)->nodes()[headline].parent_index == group,
         "headline parent is group index");
  expect((*scene)->nodes()[headline].kind == RuntimeNodeKind::text,
         "headline is text");
  expect((*scene)->nodes()[headline].text_run_index.has_value(),
         "headline text run index");
  if ((*scene)->nodes()[headline].text_run_index) {
    const auto &run =
        (*scene)->text_runs()[*(*scene)->nodes()[headline].text_run_index];
    expect(run.text == "Hello", "text payload preserved");
  }

  // Runtime scene must not expose mutable authoring objects: accessors are
  // const and the owned pointer is const CompiledScene.
  RenderJob job(std::move(*scene));
  const CompiledScene &owned = job.scene();
  expect(owned.seed() == 11, "seed preserved through ownership");
  expect(owned.nodes()[headline].transform.translate_x == 10.0,
         "translate preserved");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
