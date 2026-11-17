#pragma once

#include <instantee/core_model/ids.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/motion_grammar.hpp>
#include <instantee/direction/semantic_intent.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Concrete animation keyframe after CDS lowering. Frames are relative to the
// owning shot start (0 .. duration_frames).
struct DirectedKeyframe {
  std::uint64_t frame = 0;
  double value = 0.0;
};

// Resolved track: no semantic intent remains — only registered property ids.
struct DirectedTrack {
  std::string id;
  std::string target_id;
  std::string property;
  std::vector<DirectedKeyframe> keyframes;
};

struct DirectedShot {
  std::string id;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  std::vector<std::string> node_ids;
  std::optional<std::string> camera_node_id;
  std::vector<DirectedTrack> tracks;
  IntentKind resolved_intent = IntentKind::reveal;
};

// DirectedFilm is the CDS output: shot metadata + concrete tracks. Style
// packages contribute data only through generic lowering; no GPU types here.
struct DirectedFilm {
  std::string package_id;
  std::string package_version;
  std::uint64_t seed = 0;
  std::vector<DirectedShot> shots;
  std::vector<DirectedTrack> animation_tracks;
};

// Lower a validated semantic intent using the Motion Grammar binding for
// `package_id`. Same intent + different package → different deterministic
// tracks; unsupported package/intent bindings fail closed.
[[nodiscard]] std::expected<std::vector<DirectedTrack>, diagnostics::Error>
lower_semantic_intent(const SemanticIntent &intent,
                      std::uint64_t shot_duration_frames,
                      std::string_view shot_id, std::string_view package_id,
                      const MotionGrammarRegistry &grammar);

} // namespace instantee::direction
