#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/layering.hpp>

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

std::vector<std::string>
ids_of(const std::vector<instantee::runtime::DrawItem> &items) {
  std::vector<std::string> out;
  out.reserve(items.size());
  for (const auto &item : items) {
    out.emplace_back(item.node_id);
  }
  return out;
}

} // namespace

int main() {
  using namespace instantee::runtime;

  CompiledSceneDraft draft;
  draft.canvas_width = 1080;
  draft.canvas_height = 1920;
  draft.duration_frames = instantee::core::FrameIndex{1};
  draft.node_ids = {"group", "far_a", "near_b", "mid_c", "opaque_d", "hidden"};

  RuntimeNode group;
  group.kind = RuntimeNodeKind::group;

  RuntimeNode far_a;
  far_a.kind = RuntimeNodeKind::text;
  far_a.layer_order = 0;
  far_a.transform.translate_z = -40;
  far_a.transform.opacity = 0.5;
  far_a.blend = BlendMode::premultiplied_alpha;

  RuntimeNode near_b;
  near_b.kind = RuntimeNodeKind::text;
  near_b.layer_order = 0;
  near_b.transform.translate_z = -5;
  near_b.transform.opacity = 0.5;
  near_b.blend = BlendMode::premultiplied_alpha;

  RuntimeNode mid_c;
  mid_c.kind = RuntimeNodeKind::image;
  mid_c.layer_order = 1; // paints after layer 0
  mid_c.transform.translate_z = -100;
  mid_c.transform.opacity = 0.8;
  mid_c.blend = BlendMode::premultiplied_alpha;

  RuntimeNode opaque_d;
  opaque_d.kind = RuntimeNodeKind::geometry;
  opaque_d.layer_order = 0;
  opaque_d.transform.translate_z = -10;
  opaque_d.transform.opacity = 1.0;
  opaque_d.blend = BlendMode::opaque;

  RuntimeNode hidden;
  hidden.kind = RuntimeNodeKind::text;
  hidden.visible = false;
  hidden.transform.opacity = 0.5;
  hidden.blend = BlendMode::premultiplied_alpha;

  draft.nodes = {group, far_a, near_b, mid_c, opaque_d, hidden};

  auto scene = make_compiled_scene(std::move(draft));
  expect(scene.has_value(), "build scene");
  if (!scene) {
    return EXIT_FAILURE;
  }

  auto first = build_draw_order_from_world(**scene);
  auto second = build_draw_order_from_world(**scene);
  expect(first.has_value() && second.has_value(), "draw order");
  if (!first || !second) {
    return EXIT_FAILURE;
  }

  // Identical state → identical order.
  expect(ids_of(first->transparent_paint) == ids_of(second->transparent_paint),
         "transparent order stable");
  expect(ids_of(first->opaque_batch) == ids_of(second->opaque_batch),
         "opaque order stable");

  // Transparent back-to-front: layer 0 far before near, then layer 1.
  expect(ids_of(first->transparent_paint) ==
             std::vector<std::string>({"far_a", "near_b", "mid_c"}),
         "transparent golden order");

  // Opaque separated; hidden/group omitted.
  expect(ids_of(first->opaque_batch) == std::vector<std::string>({"opaque_d"}),
         "opaque golden order");
  expect(first->transparent_paint.size() + first->opaque_batch.size() == 4,
         "drawable count");

  // Tie-break on node_id when layer and z match.
  {
    CompiledSceneDraft tie;
    tie.canvas_width = 10;
    tie.canvas_height = 10;
    tie.duration_frames = instantee::core::FrameIndex{1};
    tie.node_ids = {"b_node", "a_node"};
    RuntimeNode left;
    left.kind = RuntimeNodeKind::text;
    left.layer_order = 0;
    left.transform.translate_z = -1;
    left.transform.opacity = 0.5;
    left.blend = BlendMode::premultiplied_alpha;
    RuntimeNode right = left;
    tie.nodes = {left, right};
    auto tie_scene = make_compiled_scene(std::move(tie));
    expect(tie_scene.has_value(), "tie scene");
    if (tie_scene) {
      auto order = build_draw_order_from_world(**tie_scene);
      expect(order.has_value(), "tie order");
      if (order) {
        expect(ids_of(order->transparent_paint) ==
                   std::vector<std::string>({"a_node", "b_node"}),
               "node_id tie-break");
      }
    }
  }

  // Opacity < 1 forces transparent path even with opaque blend mode.
  expect(is_transparent_draw(BlendMode::opaque, 0.9), "opacity forces alpha");
  expect(!is_transparent_draw(BlendMode::opaque, 1.0), "fully opaque");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
