#pragma once

#include <instantee/runtime/compiled_scene.hpp>

#include <memory>
#include <utility>

namespace instantee::runtime {

// Sole owner of a compiled scene for a render/evaluate job. Borrowed spans
// from scene() are valid only until this job releases or replaces the scene.
// Workers and GPU resources are out of scope for this type; it only encodes
// the ownership edge required by the runtime contracts.
class RenderJob {
public:
  explicit RenderJob(std::unique_ptr<const CompiledScene> scene) noexcept
      : scene_(std::move(scene)) {}

  RenderJob(const RenderJob &) = delete;
  RenderJob &operator=(const RenderJob &) = delete;
  RenderJob(RenderJob &&) noexcept = default;
  RenderJob &operator=(RenderJob &&) noexcept = default;
  ~RenderJob() = default;

  [[nodiscard]] bool has_scene() const noexcept { return scene_ != nullptr; }

  [[nodiscard]] const CompiledScene &scene() const noexcept { return *scene_; }

  [[nodiscard]] std::unique_ptr<const CompiledScene> release_scene() noexcept {
    return std::move(scene_);
  }

private:
  std::unique_ptr<const CompiledScene> scene_;
};

} // namespace instantee::runtime
