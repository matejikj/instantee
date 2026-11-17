#pragma once

#include <instantee/core_model/properties.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/document.hpp>

#include <expected>

namespace instantee::compiler {

// Compile-time binding check: every animation track property must resolve for
// a node target in the registry. Unsupported pairs fail here, not at render.
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_animation_properties(const scene_ir::SceneDocument &document,
                              const core::PropertyRegistry &registry);

} // namespace instantee::compiler
