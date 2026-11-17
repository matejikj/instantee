#include <instantee/compiler/validate_properties.hpp>

#include <utility>

namespace instantee::compiler {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message,
                       std::string path) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "compile", message}
      .with_json_path(std::move(path));
}

} // namespace

std::expected<void, diagnostics::Error>
validate_animation_properties(const scene_ir::SceneDocument &document,
                              const core::PropertyRegistry &registry) {
  for (std::size_t index = 0; index < document.animation_tracks.size();
       ++index) {
    const scene_ir::AnimationTrack &track = document.animation_tracks[index];
    const std::string path =
        "/animation_tracks/" + std::to_string(index) + "/property";
    // Scene IR animation tracks target nodes in V1.
    auto resolved = registry.resolve_for_target(
        core::AnimatableTargetKind::node, track.property);
    if (!resolved) {
      auto error = resolved.error();
      if (error.code == "unknown_property") {
        return std::unexpected(err(
            "unknown_property", "Animation property is not registered", path));
      }
      return std::unexpected(
          err("unsupported_target_property",
              "Animation property is not valid for a node target", path));
    }
  }
  return {};
}

} // namespace instantee::compiler
