#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::runtime {

// Dense, platform-neutral node kinds. No authoring enums and no GPU types.
enum class RuntimeNodeKind : std::uint8_t {
  group = 0,
  geometry,
  text,
  image,
  puppet,
  camera
};

struct RuntimeTransform {
  double translate_x = 0;
  double translate_y = 0;
  double translate_z = 0;
  double scale_x = 1;
  double scale_y = 1;
  double scale_z = 1;
  double rotation_z_rad = 0;
  double opacity = 1;
  double pivot_x = 0;
  double pivot_y = 0;
  double pivot_z = 0;
};

// Indexed node record. Parent and payload references are dense indices into
// the owning CompiledScene arrays, never string lookups on the hot path.
struct RuntimeNode {
  std::optional<std::uint32_t> parent_index;
  RuntimeNodeKind kind = RuntimeNodeKind::group;
  std::int32_t layer_order = 0;
  RuntimeTransform transform{};
  bool visible = true;
  std::optional<std::uint32_t> text_run_index;
  std::optional<std::uint32_t> camera_index;
};

struct RuntimeTextRun {
  std::string text;
  double font_size_px = 0;
  std::optional<std::string> text_role;
};

struct RuntimeCamera {
  std::string projection = "orthographic";
  double near = 0.1;
  double far = 1000.0;
  std::optional<double> fov_y_rad;
  RuntimeTransform transform{};
};

struct RuntimeShot {
  std::string id;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  std::vector<std::uint32_t> node_indices;
  std::optional<std::uint32_t> camera_node_index;
};

// Move-only construction bag. Consumed by make_compiled_scene; after success
// the logical node order is exactly nodes/node_ids order and never reshuffled
// by the id→index lookup table.
struct CompiledSceneDraft {
  std::uint32_t canvas_width = 0;
  std::uint32_t canvas_height = 0;
  std::string canvas_color = "rec709-sdr";
  core::FrameRate frame_rate = core::FrameRate::media_30_1();
  core::FrameIndex duration_frames{};
  std::uint64_t seed = 0;
  std::vector<std::string> node_ids;
  std::vector<RuntimeNode> nodes;
  std::vector<RuntimeTextRun> text_runs;
  std::vector<RuntimeCamera> cameras;
  std::vector<RuntimeShot> shots;
};

// Immutable runtime scene: indexed arrays + side lookup map. Borrowed spans
// and string_views are valid only while this object lives. No GPU handles,
// JS callbacks, or mutable authoring objects appear in this type.
class CompiledScene {
public:
  CompiledScene(const CompiledScene &) = delete;
  CompiledScene &operator=(const CompiledScene &) = delete;
  CompiledScene(CompiledScene &&) noexcept = default;
  CompiledScene &operator=(CompiledScene &&) noexcept = default;
  ~CompiledScene() = default;

  [[nodiscard]] std::uint32_t canvas_width() const noexcept {
    return canvas_width_;
  }
  [[nodiscard]] std::uint32_t canvas_height() const noexcept {
    return canvas_height_;
  }
  [[nodiscard]] std::string_view canvas_color() const noexcept {
    return canvas_color_;
  }
  [[nodiscard]] core::FrameRate frame_rate() const noexcept {
    return frame_rate_;
  }
  [[nodiscard]] core::FrameIndex duration_frames() const noexcept {
    return duration_frames_;
  }
  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

  [[nodiscard]] std::span<const std::string> node_ids() const noexcept {
    return node_ids_;
  }
  [[nodiscard]] std::span<const RuntimeNode> nodes() const noexcept {
    return nodes_;
  }
  [[nodiscard]] std::span<const RuntimeTextRun> text_runs() const noexcept {
    return text_runs_;
  }
  [[nodiscard]] std::span<const RuntimeCamera> cameras() const noexcept {
    return cameras_;
  }
  [[nodiscard]] std::span<const RuntimeShot> shots() const noexcept {
    return shots_;
  }

  // O(1) average lookup. Does not reorder nodes() / node_ids().
  [[nodiscard]] std::optional<std::uint32_t>
  find_node_index(std::string_view id) const;

  [[nodiscard]] std::optional<std::string_view>
  node_id_at(std::uint32_t index) const noexcept;

private:
  friend std::expected<std::unique_ptr<const CompiledScene>, diagnostics::Error>
  make_compiled_scene(CompiledSceneDraft draft);

  CompiledScene() = default;

  std::uint32_t canvas_width_ = 0;
  std::uint32_t canvas_height_ = 0;
  std::string canvas_color_;
  core::FrameRate frame_rate_ = core::FrameRate::media_30_1();
  core::FrameIndex duration_frames_{};
  std::uint64_t seed_ = 0;
  std::vector<std::string> node_ids_;
  std::vector<RuntimeNode> nodes_;
  std::vector<RuntimeTextRun> text_runs_;
  std::vector<RuntimeCamera> cameras_;
  std::vector<RuntimeShot> shots_;
  std::unordered_map<std::string, std::uint32_t> id_to_index_;
};

[[nodiscard]] std::expected<std::unique_ptr<const CompiledScene>,
                            diagnostics::Error>
make_compiled_scene(CompiledSceneDraft draft);

} // namespace instantee::runtime
