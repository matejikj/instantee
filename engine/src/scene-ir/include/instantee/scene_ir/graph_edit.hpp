#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/document.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::scene_ir {

// Placement among siblings under the destination parent. nullopt appends.
struct ReparentOptions {
  std::optional<std::size_t> sibling_index;
};

// Move an authoring node (and its nested children) under a new parent or to
// the shot root list. Identifiers and node payloads stay intact; only tree
// edges change. Rejects unknown ids and any move that would introduce a cycle.
// Pure data model: no UI, filesystem, or renderer dependency.
[[nodiscard]] std::expected<void, diagnostics::Error>
reparent_authoring_node(AuthoringShot &shot, std::string_view node_id,
                        std::optional<std::string_view> new_parent_id,
                        ReparentOptions options = {});

// Update Scene IR parent_id for one node. Other fields and every node id are
// preserved. Rejects unknown ids and cyclic parents.
[[nodiscard]] std::expected<void, diagnostics::Error>
reparent_scene_node(SceneDocument &document, std::string_view node_id,
                    std::optional<std::string_view> new_parent_id);

// Immediate parent id in the authoring tree, or nullopt at shot root / missing.
[[nodiscard]] std::optional<std::string>
authoring_parent_id(const AuthoringShot &shot, std::string_view node_id);

// Depth-first ids in document order (shot roots, then nested children).
[[nodiscard]] std::vector<std::string>
collect_authoring_ids(const AuthoringShot &shot);

} // namespace instantee::scene_ir
