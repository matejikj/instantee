#include <instantee/runtime/clip_mask.hpp>
#include <instantee/runtime/frame_evaluator.hpp>
#include <instantee/runtime/transforms.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_near(double actual, double expected, double tol,
                 const char *message) {
  if (!(std::isfinite(actual) && std::abs(actual - expected) <= tol)) {
    std::cerr << "FAIL: " << message << " got " << actual << " expected "
              << expected << '\n';
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
  draft.duration_frames = FrameIndex{30};
  draft.seed = 7;
  draft.node_ids = {"root", "group", "leaf", "mask_src"};

  RuntimeNode root;
  root.kind = RuntimeNodeKind::group;
  root.transform.translate_x = 100.0;

  RuntimeNode group;
  group.kind = RuntimeNodeKind::group;
  group.parent_index = 0;
  group.transform.translate_x = 20.0;

  RuntimeNode leaf;
  leaf.kind = RuntimeNodeKind::geometry;
  leaf.parent_index = 1;
  leaf.transform.translate_y = 5.0;

  RuntimeNode mask_src;
  mask_src.kind = RuntimeNodeKind::geometry;
  mask_src.parent_index = 0;

  draft.nodes = {root, group, leaf, mask_src};
  auto scene = make_compiled_scene(std::move(draft));
  expect(scene.has_value(), "compiled scene");
  if (!scene) {
    return EXIT_FAILURE;
  }

  auto worlds = compute_world_matrices(**scene);
  expect(worlds.has_value(), "world matrices");
  if (!worlds) {
    return EXIT_FAILURE;
  }

  // Parent clip intersects with child clip in world space.
  ClipMaskProgram program;
  program.bindings.push_back(NodeClipMaskBinding{
      .node_index = 0,
      .local_clip = ClipRect{0.0, 0.0, 80.0, 60.0},
      .mask_node_index = 3,
  });
  program.bindings.push_back(NodeClipMaskBinding{
      .node_index = 1,
      .local_clip = ClipRect{-10.0, -10.0, 40.0, 40.0},
      .mask_node_index = std::nullopt,
  });

  auto resolved = resolve_clip_masks(**scene, program, *worlds);
  expect(resolved.has_value(), "resolve clip masks");
  if (!resolved) {
    std::cerr << resolved.error().message << '\n';
    return EXIT_FAILURE;
  }

  // Root world clip: local [0,80]x[0,60] translated by +100 on X.
  expect((*resolved)[0].world_clip.has_value(), "root has world clip");
  if ((*resolved)[0].world_clip) {
    expect_near((*resolved)[0].world_clip->min_x, 100.0, 1e-9, "root min_x");
    expect_near((*resolved)[0].world_clip->max_x, 180.0, 1e-9, "root max_x");
    expect_near((*resolved)[0].world_clip->min_y, 0.0, 1e-9, "root min_y");
    expect_near((*resolved)[0].world_clip->max_y, 60.0, 1e-9, "root max_y");
  }
  expect((*resolved)[0].mask_node_index.has_value() &&
             *(*resolved)[0].mask_node_index == 3,
         "root mask is mask_src");

  // Group inherits root mask; clip is intersection of root and group clips.
  // Group local clip [-10,40]x[-10,40] at world x=120 → [110,160]x[-10,40].
  // Intersect with root [100,180]x[0,60] → [110,160]x[0,40].
  expect((*resolved)[1].mask_node_index.has_value() &&
             *(*resolved)[1].mask_node_index == 3,
         "group inherits root mask");
  expect(!(*resolved)[1].clip_empty, "group clip not empty");
  expect((*resolved)[1].world_clip.has_value(), "group has world clip");
  if ((*resolved)[1].world_clip) {
    expect_near((*resolved)[1].world_clip->min_x, 110.0, 1e-9, "group min_x");
    expect_near((*resolved)[1].world_clip->max_x, 160.0, 1e-9, "group max_x");
    expect_near((*resolved)[1].world_clip->min_y, 0.0, 1e-9, "group min_y");
    expect_near((*resolved)[1].world_clip->max_y, 40.0, 1e-9, "group max_y");
  }

  // Leaf inherits same mask and the intersected group/root clip.
  expect((*resolved)[2].mask_node_index.has_value() &&
             *(*resolved)[2].mask_node_index == 3,
         "leaf inherits root mask");
  expect((*resolved)[2].world_clip.has_value(), "leaf has inherited clip");
  if ((*resolved)[2].world_clip && (*resolved)[1].world_clip) {
    expect_near((*resolved)[2].world_clip->min_x,
                (*resolved)[1].world_clip->min_x, 1e-9, "leaf clip min_x");
    expect_near((*resolved)[2].world_clip->max_x,
                (*resolved)[1].world_clip->max_x, 1e-9, "leaf clip max_x");
  }
  expect((*resolved)[2].inherited_visible, "leaf inherited visible");

  // Child mask binding overrides the inherited ancestor mask.
  ClipMaskProgram override_program;
  override_program.bindings.push_back(NodeClipMaskBinding{
      .node_index = 0,
      .local_clip = std::nullopt,
      .mask_node_index = 3,
  });
  override_program.bindings.push_back(NodeClipMaskBinding{
      .node_index = 2,
      .local_clip = std::nullopt,
      .mask_node_index = 1,
  });
  auto overridden = resolve_clip_masks(**scene, override_program, *worlds);
  expect(overridden.has_value(), "override resolve");
  if (overridden) {
    expect((*overridden)[2].mask_node_index.has_value() &&
               *(*overridden)[2].mask_node_index == 1,
           "leaf mask overrides ancestor");
    expect((*overridden)[1].mask_node_index.has_value() &&
               *(*overridden)[1].mask_node_index == 3,
           "group still inherits root mask");
  }

  // Disjoint clips → clip_empty on the descendant.
  ClipMaskProgram disjoint;
  disjoint.bindings.push_back(NodeClipMaskBinding{
      .node_index = 0,
      .local_clip = ClipRect{0.0, 0.0, 10.0, 10.0},
  });
  disjoint.bindings.push_back(NodeClipMaskBinding{
      .node_index = 1,
      .local_clip = ClipRect{200.0, 200.0, 210.0, 210.0},
  });
  auto empty_hit = resolve_clip_masks(**scene, disjoint, *worlds);
  expect(empty_hit.has_value(), "disjoint resolve");
  if (empty_hit) {
    expect((*empty_hit)[1].clip_empty, "group clip_empty when disjoint");
    expect(!(*empty_hit)[1].world_clip.has_value(),
           "group world_clip cleared when empty");
    expect((*empty_hit)[2].clip_empty,
           "leaf clip_empty when ancestors disjoint");
  }

  // Invisible ancestor clears inherited_visible.
  {
    CompiledSceneDraft hidden_draft = {};
    hidden_draft.canvas_width = 100;
    hidden_draft.canvas_height = 100;
    hidden_draft.duration_frames = FrameIndex{1};
    hidden_draft.node_ids = {"a", "b"};
    RuntimeNode a;
    a.kind = RuntimeNodeKind::group;
    a.visible = false;
    RuntimeNode b;
    b.kind = RuntimeNodeKind::geometry;
    b.parent_index = 0;
    b.visible = true;
    hidden_draft.nodes = {a, b};
    auto hidden_scene = make_compiled_scene(std::move(hidden_draft));
    expect(hidden_scene.has_value(), "hidden scene");
    if (hidden_scene) {
      auto hw = compute_world_matrices(**hidden_scene);
      expect(hw.has_value(), "hidden worlds");
      ClipMaskProgram empty_prog;
      auto hv = resolve_clip_masks(**hidden_scene, empty_prog, *hw);
      expect(hv.has_value(), "hidden resolve");
      if (hv) {
        expect(!(*hv)[0].inherited_visible, "hidden root not visible");
        expect(!(*hv)[1].inherited_visible,
               "child inherits ancestor invisibility");
      }
    }
  }

  // Invalid references are rejected.
  ClipMaskProgram bad_node;
  bad_node.bindings.push_back(NodeClipMaskBinding{
      .node_index = 99,
      .local_clip = ClipRect{0, 0, 1, 1},
  });
  expect(!validate_clip_mask_program(**scene, bad_node).has_value(),
         "reject out-of-range node_index");
  expect(!resolve_clip_masks(**scene, bad_node, *worlds).has_value(),
         "resolve rejects out-of-range node_index");

  ClipMaskProgram bad_mask;
  bad_mask.bindings.push_back(NodeClipMaskBinding{
      .node_index = 0,
      .mask_node_index = 99,
  });
  auto bad_mask_result = validate_clip_mask_program(**scene, bad_mask);
  expect(!bad_mask_result.has_value(), "reject out-of-range mask_node_index");
  if (!bad_mask_result) {
    expect(bad_mask_result.error().code == "dangling_reference",
           "mask error code dangling_reference");
  }

  ClipMaskProgram bad_rect;
  bad_rect.bindings.push_back(NodeClipMaskBinding{
      .node_index = 0,
      .local_clip = ClipRect{10.0, 0.0, 0.0, 5.0},
  });
  expect(!validate_clip_mask_program(**scene, bad_rect).has_value(),
         "reject denormalized clip rect");

  ClipMaskProgram dup;
  dup.bindings.push_back(
      NodeClipMaskBinding{.node_index = 0, .mask_node_index = 3});
  dup.bindings.push_back(
      NodeClipMaskBinding{.node_index = 0, .mask_node_index = 1});
  expect(!validate_clip_mask_program(**scene, dup).has_value(),
         "reject duplicate node bindings");

  ClipMaskProgram empty_binding;
  empty_binding.bindings.push_back(NodeClipMaskBinding{.node_index = 0});
  expect(!validate_clip_mask_program(**scene, empty_binding).has_value(),
         "reject binding with neither clip nor mask");

  // Frame evaluator publishes clip/mask into NodeFrameState.
  AnimationProgram anim;
  auto framed = evaluate_frame(**scene, anim, program, FrameIndex{0});
  expect(framed.has_value(), "evaluate_frame with clips");
  if (framed) {
    expect(framed->nodes[2].mask_node_index.has_value() &&
               *framed->nodes[2].mask_node_index == 3,
           "FrameState leaf inherits mask");
    expect(framed->nodes[2].world_clip.has_value(),
           "FrameState leaf has world clip");
    expect(framed->nodes[2].inherited_visible, "FrameState leaf visible");
  }

  // evaluate_frame without clips still propagates inherited_visible.
  {
    CompiledSceneDraft inv_draft;
    inv_draft.canvas_width = 10;
    inv_draft.canvas_height = 10;
    inv_draft.duration_frames = FrameIndex{1};
    inv_draft.node_ids = {"p", "c"};
    RuntimeNode p;
    p.visible = false;
    RuntimeNode c;
    c.parent_index = 0;
    c.visible = true;
    inv_draft.nodes = {p, c};
    auto inv_scene = make_compiled_scene(std::move(inv_draft));
    expect(inv_scene.has_value(), "inv scene");
    if (inv_scene) {
      AnimationProgram empty_anim;
      auto st = evaluate_frame(**inv_scene, empty_anim, FrameIndex{0});
      expect(st.has_value(), "evaluate without clips");
      if (st) {
        expect(st->nodes[1].visible, "local visible stays true");
        expect(!st->nodes[1].inherited_visible,
               "inherited_visible false without clip program");
      }
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
