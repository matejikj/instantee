#include <instantee/scene_ir/graph_edit.hpp>

#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message,
                       std::string json_path) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "scene_ir", message}
      .with_json_path(std::move(json_path));
}

bool subtree_contains_id(const AuthoringNode &node, std::string_view id) {
  if (node.id == id) {
    return true;
  }
  for (const AuthoringNode &child : node.children) {
    if (subtree_contains_id(child, id)) {
      return true;
    }
  }
  return false;
}

struct NodeLocate {
  std::vector<AuthoringNode> *siblings = nullptr;
  std::size_t index = 0;
};

bool locate_node(std::vector<AuthoringNode> &nodes, std::string_view id,
                 NodeLocate &out) {
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (nodes[index].id == id) {
      out.siblings = &nodes;
      out.index = index;
      return true;
    }
    if (locate_node(nodes[index].children, id, out)) {
      return true;
    }
  }
  return false;
}

AuthoringNode *find_node_mut(std::vector<AuthoringNode> &nodes,
                             std::string_view id) {
  for (AuthoringNode &node : nodes) {
    if (node.id == id) {
      return &node;
    }
    if (AuthoringNode *nested = find_node_mut(node.children, id)) {
      return nested;
    }
  }
  return nullptr;
}

void collect_ids(const AuthoringNode &node, std::vector<std::string> &out) {
  out.push_back(node.id);
  for (const AuthoringNode &child : node.children) {
    collect_ids(child, out);
  }
}

// Tri-state parent lookup: missing node vs root vs nested parent.
struct ParentLookup {
  bool found = false;
  std::optional<std::string> parent_id;
};

ParentLookup lookup_parent(const std::vector<AuthoringNode> &nodes,
                           std::string_view id, const std::string *parent) {
  for (const AuthoringNode &node : nodes) {
    if (node.id == id) {
      ParentLookup hit;
      hit.found = true;
      if (parent != nullptr) {
        hit.parent_id = *parent;
      }
      return hit;
    }
    ParentLookup nested = lookup_parent(node.children, id, &node.id);
    if (nested.found) {
      return nested;
    }
  }
  return {};
}

} // namespace

std::vector<std::string> collect_authoring_ids(const AuthoringShot &shot) {
  std::vector<std::string> ids;
  for (const AuthoringNode &node : shot.nodes) {
    collect_ids(node, ids);
  }
  return ids;
}

std::optional<std::string> authoring_parent_id(const AuthoringShot &shot,
                                               std::string_view node_id) {
  const ParentLookup hit = lookup_parent(shot.nodes, node_id, nullptr);
  if (!hit.found) {
    return std::nullopt;
  }
  return hit.parent_id;
}

std::expected<void, diagnostics::Error>
reparent_authoring_node(AuthoringShot &shot, std::string_view node_id,
                        std::optional<std::string_view> new_parent_id,
                        ReparentOptions options) {
  if (node_id.empty()) {
    return std::unexpected(
        err("invalid_id", "node_id must be non-empty", "/nodes"));
  }
  if (new_parent_id.has_value() && new_parent_id->empty()) {
    return std::unexpected(
        err("invalid_id", "new_parent_id must be non-empty", "/nodes"));
  }
  if (new_parent_id.has_value() && *new_parent_id == node_id) {
    return std::unexpected(
        err("cyclic_parent", "A node cannot be its own parent", "/nodes")
            .with_node(std::string(node_id)));
  }

  NodeLocate source{};
  if (!locate_node(shot.nodes, node_id, source)) {
    return std::unexpected(
        err("unknown_node", "Node id not found in shot", "/nodes")
            .with_node(std::string(node_id)));
  }

  std::vector<AuthoringNode> *destination = &shot.nodes;
  if (new_parent_id.has_value()) {
    AuthoringNode *parent = find_node_mut(shot.nodes, *new_parent_id);
    if (parent == nullptr) {
      return std::unexpected(
          err("unknown_parent", "Parent id not found in shot", "/nodes")
              .with_node(std::string(*new_parent_id)));
    }
    if (subtree_contains_id(source.siblings->at(source.index),
                            *new_parent_id)) {
      return std::unexpected(
          err("cyclic_parent",
              "Cannot reparent a node under its own descendant", "/nodes")
              .with_node(std::string(node_id)));
    }
    destination = &parent->children;
  }

  std::size_t insert_at = destination->size();
  if (options.sibling_index.has_value()) {
    if (*options.sibling_index > destination->size()) {
      return std::unexpected(
          err("invalid_index", "sibling_index exceeds destination child count",
              "/nodes"));
    }
    insert_at = *options.sibling_index;
  }

  // Same-container reorder: adjust index after the upcoming erase.
  if (destination == source.siblings && insert_at > source.index) {
    --insert_at;
  }
  if (destination == source.siblings && insert_at == source.index &&
      options.sibling_index.has_value()) {
    // No-op move to the same slot.
    return {};
  }

  AuthoringNode moved = std::move(source.siblings->at(source.index));
  source.siblings->erase(source.siblings->begin() +
                         static_cast<std::ptrdiff_t>(source.index));
  // After erase, destination may need a refreshed insert bound when it was the
  // same vector; insert_at already adjusted above.
  if (insert_at > destination->size()) {
    insert_at = destination->size();
  }
  destination->insert(destination->begin() +
                          static_cast<std::ptrdiff_t>(insert_at),
                      std::move(moved));
  return {};
}

std::expected<void, diagnostics::Error>
reparent_scene_node(SceneDocument &document, std::string_view node_id,
                    std::optional<std::string_view> new_parent_id) {
  if (node_id.empty()) {
    return std::unexpected(
        err("invalid_id", "node_id must be non-empty", "/nodes"));
  }
  if (new_parent_id.has_value() && new_parent_id->empty()) {
    return std::unexpected(
        err("invalid_id", "new_parent_id must be non-empty", "/nodes"));
  }
  if (new_parent_id.has_value() && *new_parent_id == node_id) {
    return std::unexpected(
        err("cyclic_parent", "A node cannot be its own parent", "/nodes")
            .with_node(std::string(node_id)));
  }

  Node *target = nullptr;
  for (Node &node : document.nodes) {
    if (node.id == node_id) {
      target = &node;
      break;
    }
  }
  if (target == nullptr) {
    return std::unexpected(err("unknown_node", "Node id not found", "/nodes")
                               .with_node(std::string(node_id)));
  }

  if (new_parent_id.has_value()) {
    const Node *parent = nullptr;
    for (const Node &node : document.nodes) {
      if (node.id == *new_parent_id) {
        parent = &node;
        break;
      }
    }
    if (parent == nullptr) {
      return std::unexpected(
          err("unknown_parent", "Parent id not found", "/nodes")
              .with_node(std::string(*new_parent_id)));
    }

    // Walk ancestors of the proposed parent; hitting node_id would cycle.
    std::optional<std::string_view> cursor = *new_parent_id;
    while (cursor.has_value()) {
      if (*cursor == node_id) {
        return std::unexpected(
            err("cyclic_parent",
                "Cannot reparent a node under its own descendant", "/nodes")
                .with_node(std::string(node_id)));
      }
      const Node *cursor_node = nullptr;
      for (const Node &node : document.nodes) {
        if (node.id == *cursor) {
          cursor_node = &node;
          break;
        }
      }
      if (cursor_node == nullptr || !cursor_node->parent_id.has_value()) {
        break;
      }
      cursor = *cursor_node->parent_id;
    }
  }

  if (new_parent_id.has_value()) {
    target->parent_id = std::string(*new_parent_id);
  } else {
    target->parent_id = std::nullopt;
  }
  return {};
}

} // namespace instantee::scene_ir
