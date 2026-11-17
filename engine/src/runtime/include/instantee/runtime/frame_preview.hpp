#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/clip_mask.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/frame_evaluator.hpp>

#include <cstdint>
#include <expected>

namespace instantee::runtime {

// Result of a single random-access logical preview. `evaluations` is always 1:
// FrameState(n) never walks 0..n-1 (SAS FR-03 / FR-14).
struct FramePreview {
  FrameState state{};
  std::uint64_t evaluations = 1;
};

// Evaluate absolute film frame `at` for a subsequent renderer or CLI. Rejects
// `at >= scene.duration_frames()`. Does not mutate scene or program.
[[nodiscard]] std::expected<FramePreview, diagnostics::Error>
preview_frame_at(const CompiledScene &scene, const AnimationProgram &program,
                 core::FrameIndex at);

[[nodiscard]] std::expected<FramePreview, diagnostics::Error>
preview_frame_at(const CompiledScene &scene, const AnimationProgram &program,
                 const ClipMaskProgram &clips, core::FrameIndex at);

} // namespace instantee::runtime
