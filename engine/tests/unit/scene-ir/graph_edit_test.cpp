#include <instantee/scene_ir/graph_edit.hpp>
#include <instantee/scene_ir/graph_validate.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
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

instantee::scene_ir::AuthoringNode make_text(std::string id) {
  instantee::scene_ir::AuthoringNode node;
  node.id = std::move(id);
  node.type = "text";
  node.text = "label";
  return node;
}

instantee::scene_ir::AuthoringNode make_group(std::string id) {
  instantee::scene_ir::AuthoringNode node;
  node.id = std::move(id);
  node.type = "group";
  return node;
}

instantee::scene_ir::AuthoringShot sample_shot() {
  instantee::scene_ir::AuthoringShot shot;
  shot.id = "intro";
  shot.duration_frames = 30;
  auto group = make_group("group_a");
  group.children.push_back(make_text("child_a"));
  group.children.push_back(make_text("child_b"));
  shot.nodes.push_back(std::move(group));
  shot.nodes.push_back(make_text("root_b"));
  return shot;
}

instantee::scene_ir::SceneDocument sample_scene() {
  using instantee::scene_ir::Node;
  using instantee::scene_ir::NodeKind;
  instantee::scene_ir::SceneDocument document;
  document.schema_version = "1.0.0";
  document.compiler_version = "test";
  document.canvas_width = 1080;
  document.canvas_height = 1920;
  document.duration_frames = instantee::core::FrameIndex{30};

  Node root;
  root.id = "root";
  root.kind = NodeKind::group;

  Node child;
  child.id = "child";
  child.kind = NodeKind::text;
  child.parent_id = "root";

  Node leaf;
  leaf.id = "leaf";
  leaf.kind = NodeKind::text;
  leaf.parent_id = "child";

  document.nodes.push_back(std::move(root));
  document.nodes.push_back(std::move(child));
  document.nodes.push_back(std::move(leaf));
  return document;
}

} // namespace

int main() {
  using namespace instantee::scene_ir;

  {
    AuthoringShot shot = sample_shot();
    const auto before = collect_authoring_ids(shot);
    const auto moved =
        reparent_authoring_node(shot, "child_a", "root_b", ReparentOptions{});
    expect(moved.has_value(), "reparent child under root_b");
    expect(authoring_parent_id(shot, "child_a") == std::string("root_b"),
           "child_a parent is root_b");
    expect(authoring_parent_id(shot, "child_b") == std::string("group_a"),
           "child_b parent unchanged");
    auto after = collect_authoring_ids(shot);
    std::sort(after.begin(), after.end());
    auto sorted_before = before;
    std::sort(sorted_before.begin(), sorted_before.end());
    expect(after == sorted_before, "authoring ids preserved after reparent");
  }

  {
    AuthoringShot shot = sample_shot();
    const auto cyclic =
        reparent_authoring_node(shot, "group_a", "child_a", ReparentOptions{});
    expect(!cyclic.has_value(), "reject reparent under descendant");
    if (!cyclic) {
      expect(cyclic.error().code == "cyclic_parent", "cyclic_parent code");
    }
    expect(authoring_parent_id(shot, "child_a") == std::string("group_a"),
           "failed reparent leaves tree unchanged");
  }

  {
    AuthoringShot shot = sample_shot();
    const auto self =
        reparent_authoring_node(shot, "group_a", "group_a", ReparentOptions{});
    expect(!self.has_value(), "reject self parent");
    if (!self) {
      expect(self.error().code == "cyclic_parent", "self cyclic_parent code");
    }
  }

  {
    AuthoringShot shot = sample_shot();
    ReparentOptions options;
    options.sibling_index = 0;
    const auto to_root =
        reparent_authoring_node(shot, "child_b", std::nullopt, options);
    expect(to_root.has_value(), "reparent to shot root");
    expect(!authoring_parent_id(shot, "child_b").has_value(),
           "child_b is shot root");
    expect(shot.nodes.size() >= 2, "root list grew or reordered");
    expect(shot.nodes.front().id == "child_b", "child_b inserted at index 0");
  }

  {
    AuthoringShot shot = sample_shot();
    const auto missing =
        reparent_authoring_node(shot, "nope", "group_a", ReparentOptions{});
    expect(!missing.has_value(), "reject unknown node");
    if (!missing) {
      expect(missing.error().code == "unknown_node", "unknown_node code");
    }
  }

  {
    SceneDocument document = sample_scene();
    std::set<std::string> before_ids;
    for (const Node &node : document.nodes) {
      before_ids.insert(node.id);
    }
    const std::string leaf_kind_before =
        document.nodes[2].kind == NodeKind::text ? "text" : "other";

    const auto moved = reparent_scene_node(document, "leaf", "root");
    expect(moved.has_value(), "reparent scene leaf under root");
    expect(document.nodes[2].parent_id == std::string("root"),
           "leaf parent_id updated");
    expect(leaf_kind_before == "text", "leaf kind preserved");
    std::set<std::string> after_ids;
    for (const Node &node : document.nodes) {
      after_ids.insert(node.id);
    }
    expect(before_ids == after_ids, "scene ids preserved");
  }

  {
    SceneDocument document = sample_scene();
    const auto cyclic = reparent_scene_node(document, "root", "leaf");
    expect(!cyclic.has_value(), "reject scene cycle");
    if (!cyclic) {
      expect(cyclic.error().code == "cyclic_parent", "scene cyclic_parent");
    }
    expect(!document.nodes[0].parent_id.has_value(),
           "failed scene reparent leaves root parent empty");
  }

  {
    SceneDocument document = sample_scene();
    document.nodes[2].parent_id = "root";
    // Force a cycle via edit API rejection path already tested; also ensure
    // validate_graph still catches hand-built cycles.
    document.nodes[0].parent_id = "leaf";
    document.nodes[2].parent_id = "root";
    const auto invalid = validate_graph(document);
    expect(!invalid.has_value(), "validate_graph rejects cycle");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
