#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/render_job.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

instantee::runtime::CompiledSceneDraft sample_draft() {
  using namespace instantee::runtime;
  CompiledSceneDraft draft;
  draft.canvas_width = 1080;
  draft.canvas_height = 1920;
  draft.duration_frames = instantee::core::FrameIndex{30};
  draft.seed = 7;
  draft.node_ids = {"root", "child"};
  RuntimeNode root;
  root.kind = RuntimeNodeKind::group;
  root.layer_order = 0;
  RuntimeNode child;
  child.kind = RuntimeNodeKind::text;
  child.parent_index = 0;
  child.layer_order = 1;
  child.text_run_index = 0;
  draft.nodes = {root, child};
  RuntimeTextRun run;
  run.text = "hello";
  run.font_size_px = 48;
  draft.text_runs = {std::move(run)};
  RuntimeShot shot;
  shot.id = "intro";
  shot.duration_frames = 30;
  shot.node_indices = {0, 1};
  draft.shots = {std::move(shot)};
  return draft;
}

} // namespace

int main() {
  using namespace instantee::runtime;

  {
    auto scene = make_compiled_scene(sample_draft());
    expect(scene.has_value(), "make_compiled_scene accepts sample");
    if (!scene) {
      return EXIT_FAILURE;
    }
    expect((*scene)->nodes().size() == 2, "two nodes");
    expect((*scene)->node_ids()[0] == "root", "logical order root first");
    expect((*scene)->node_ids()[1] == "child", "logical order child second");
    const auto root_index = (*scene)->find_node_index("root");
    const auto child_index = (*scene)->find_node_index("child");
    expect(root_index == 0u, "lookup root index 0");
    expect(child_index == 1u, "lookup child index 1");
    expect((*scene)->nodes()[1].parent_index == 0u, "child parent index");
    expect(!(*scene)->find_node_index("missing").has_value(),
           "missing id yields nullopt");

    // Lookup must not reshuffle the logical arrays.
    const std::vector<std::string> before((*scene)->node_ids().begin(),
                                          (*scene)->node_ids().end());
    (void)(*scene)->find_node_index("child");
    (void)(*scene)->find_node_index("root");
    const std::vector<std::string> after((*scene)->node_ids().begin(),
                                         (*scene)->node_ids().end());
    expect(before == after, "lookup preserves node_ids order");
  }

  {
    CompiledSceneDraft draft = sample_draft();
    draft.nodes[0].parent_index = 1;
    draft.nodes[1].parent_index = 0;
    const auto cyclic = make_compiled_scene(std::move(draft));
    expect(!cyclic.has_value(), "reject cyclic parents");
    if (!cyclic) {
      expect(cyclic.error().code == "cyclic_parent", "cyclic_parent code");
    }
  }

  {
    CompiledSceneDraft draft = sample_draft();
    draft.node_ids.push_back("extra");
    const auto mismatch = make_compiled_scene(std::move(draft));
    expect(!mismatch.has_value(), "reject length mismatch");
  }

  {
    auto scene = make_compiled_scene(sample_draft());
    expect(scene.has_value(), "scene for RenderJob");
    if (!scene) {
      return EXIT_FAILURE;
    }
    RenderJob job(std::move(*scene));
    expect(job.has_scene(), "RenderJob owns scene");
    expect(job.scene().nodes().size() == 2, "borrowed nodes span");
    const auto released = job.release_scene();
    expect(released != nullptr, "release transfers ownership");
    expect(!job.has_scene(), "RenderJob empty after release");
  }

  // CompiledScene public API is const-only accessors; mutation would require
  // casting away const on the unique_ptr target, which RenderJob does not
  // expose. Authoring headers are not part of this translation unit.

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
