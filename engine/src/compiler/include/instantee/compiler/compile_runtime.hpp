#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/frame_evaluator.hpp>
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

// Lower Scene IR scalar animation tracks onto CompiledScene node indices.
// Unsupported property names are skipped (not silent creative intents—only
// channels the runtime sampler already understands). Empty input yields an
// empty program.
[[nodiscard]] std::expected<runtime::AnimationProgram, diagnostics::Error>
compile_animation_program(const scene_ir::SceneDocument &document,
                          const runtime::CompiledScene &scene);

} // namespace instantee::compiler
