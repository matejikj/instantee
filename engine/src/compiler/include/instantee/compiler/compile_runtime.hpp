#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/scene_ir/document.hpp>

#include <expected>
#include <memory>

namespace instantee::compiler {

// Lower a validated Scene IR document into an immutable indexed runtime scene.
// String parent/target ids become dense indices. The result owns no GPU state
// and embeds no authoring objects or JS callbacks.
[[nodiscard]] std::expected<std::unique_ptr<const runtime::CompiledScene>,
                            diagnostics::Error>
compile_runtime_scene(const scene_ir::SceneDocument &document);

} // namespace instantee::compiler
