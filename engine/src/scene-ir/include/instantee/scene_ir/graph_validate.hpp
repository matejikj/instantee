#pragma once

#include <instantee/scene_ir/document.hpp>

#include <expected>

namespace instantee::scene_ir {

// Uniqueness, typed references, acyclic parents, shot coverage and keyframe
// bounds. Deterministic: same document always yields the same Error snapshot.
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_graph(const SceneDocument &document);

} // namespace instantee::scene_ir
