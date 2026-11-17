#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/transforms.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::core;
  using namespace instantee::runtime;

  CompiledSceneDraft draft;
  draft.canvas_width = 1080;
  draft.canvas_height = 1920;
  draft.duration_frames = FrameIndex{1};
  draft.node_ids = {"root", "child", "leaf"};

  RuntimeNode root;
  root.kind = RuntimeNodeKind::group;
  root.transform.translate_x = 10;
  root.transform.translate_y = 20;

  RuntimeNode child;
  child.kind = RuntimeNodeKind::group;
  child.parent_index = 0;
  child.transform.translate_x = 3;
  child.transform.rotation_z_rad = std::numbers::pi_v<double> / 2.0;

  RuntimeNode leaf;
  leaf.kind = RuntimeNodeKind::text;
  leaf.parent_index = 1;
  leaf.transform.translate_y = 5;
  leaf.transform.pivot_x = 1;
  leaf.transform.scale_x = 2;
  leaf.transform.scale_y = 2;

  draft.nodes = {root, child, leaf};
  auto scene = make_compiled_scene(std::move(draft));
  expect(scene.has_value(), "build scene");
  if (!scene) {
    return EXIT_FAILURE;
  }

  auto worlds = compute_world_matrices(**scene);
  expect(worlds.has_value(), "compute world matrices");
  if (!worlds) {
    return EXIT_FAILURE;
  }
  expect(worlds->size() == 3, "one matrix per node");

  // Root local == world.
  expect(nearly_equal((*worlds)[0], local_matrix(root.transform)),
         "root world equals local");

  // Child origin in world: parent T(10,20) * child T(3,0)*R → (13,20).
  expect(nearly_equal(transform_point((*worlds)[1], Vec3{0, 0, 0}),
                      Vec3{13, 20, 0}),
         "child origin under translated parent");

  // Reference: manually combine parent*local for leaf.
  const Mat4 expected_leaf =
      combine_parent_local((*worlds)[1], local_matrix(leaf.transform));
  expect(nearly_equal((*worlds)[2], expected_leaf),
         "leaf world matches parent*local");

  // Hierarchy: moving parent affects child point.
  const Vec3 leaf_origin = transform_point((*worlds)[2], Vec3{0, 0, 0});
  expect(leaf_origin.x != 0 || leaf_origin.y != 0, "leaf not at world origin");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
