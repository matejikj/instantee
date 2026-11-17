#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/directed_film.hpp>
#include <instantee/direction/shot_planner.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Declared camera moves a director may request. Concrete tracks are emitted
// only for capabilities present in the director's allow-list.
enum class CameraCapability : std::uint8_t {
  dolly = 1,
  pan,
  zoom,
  focus,
};

class CameraCapabilitySet {
public:
  CameraCapabilitySet() = default;

  [[nodiscard]] static CameraCapabilitySet none() { return {}; }
  [[nodiscard]] static CameraCapabilitySet all_v1();

  [[nodiscard]] bool contains(CameraCapability capability) const noexcept;
  void allow(CameraCapability capability) noexcept;
  void revoke(CameraCapability capability) noexcept;

  [[nodiscard]] std::vector<CameraCapability> list() const;

private:
  std::uint8_t mask_ = 0;
};

struct CameraLanguageRequest {
  ShotPlan shot{};
  std::string camera_node_id;
  CameraCapabilitySet allowed = CameraCapabilitySet::all_v1();
  // Peak amplitude scale in [0, 1]; combined with framing-specific ranges.
  double intensity = 0.5;
};

// Required capabilities implied by a framing choice. Directors that omit any
// of these fail closed — they may not invent undeclared camera moves.
[[nodiscard]] std::vector<CameraCapability>
required_capabilities_for(CameraFraming framing);

// Lower shot framing into concrete camera-node animation tracks. Keyframe
// frames are relative to the shot start and stay inside the author budget.
[[nodiscard]] std::expected<std::vector<DirectedTrack>, diagnostics::Error>
plan_camera_language(const CameraLanguageRequest &request);

[[nodiscard]] constexpr std::string_view
camera_capability_id(CameraCapability capability) noexcept {
  switch (capability) {
  case CameraCapability::dolly:
    return "dolly";
  case CameraCapability::pan:
    return "pan";
  case CameraCapability::zoom:
    return "zoom";
  case CameraCapability::focus:
    return "focus";
  }
  return "unknown";
}

[[nodiscard]] std::expected<CameraCapability, diagnostics::Error>
parse_camera_capability(std::string_view text);

} // namespace instantee::direction
