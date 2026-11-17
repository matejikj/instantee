#include <instantee/runtime/frame_preview.hpp>

#include <string>
#include <string_view>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

diagnostics::Error out_of_range(core::FrameIndex at,
                                core::FrameIndex duration) {
  return err("frame_out_of_range",
             "Preview frame index is outside [0, duration_frames); duration=" +
                 std::to_string(duration.value()))
      .with_frame(at.value());
}

} // namespace

std::expected<FramePreview, diagnostics::Error>
preview_frame_at(const CompiledScene &scene, const AnimationProgram &program,
                 core::FrameIndex at) {
  if (!(at < scene.duration_frames())) {
    return std::unexpected(out_of_range(at, scene.duration_frames()));
  }
  auto state = evaluate_frame(scene, program, at);
  if (!state) {
    return std::unexpected(state.error());
  }
  return FramePreview{.state = std::move(*state), .evaluations = 1};
}

std::expected<FramePreview, diagnostics::Error>
preview_frame_at(const CompiledScene &scene, const AnimationProgram &program,
                 const ClipMaskProgram &clips, core::FrameIndex at) {
  if (!(at < scene.duration_frames())) {
    return std::unexpected(out_of_range(at, scene.duration_frames()));
  }
  auto state = evaluate_frame(scene, program, clips, at);
  if (!state) {
    return std::unexpected(state.error());
  }
  return FramePreview{.state = std::move(*state), .evaluations = 1};
}

} // namespace instantee::runtime
