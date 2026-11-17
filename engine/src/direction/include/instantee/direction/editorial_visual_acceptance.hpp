#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/safe_area.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Pixel/channel tolerances for editorial reference-frame comparison.
// Subjective impression alone is never an acceptance path: callers must bind
// checklist results, reference digests, and these tolerances.
struct VisualTolerance {
  std::uint8_t max_channel_delta = 0;
  // Fraction of pixels allowed to exceed max_channel_delta. Must be in [0, 1].
  double max_mismatch_ratio = 0.0;
};

struct ImageCompareStats {
  std::uint64_t pixels = 0;
  std::uint64_t mismatched_pixels = 0;
  std::uint8_t max_observed_channel_delta = 0;
  bool within_tolerance = false;
};

struct VisualChecklistItem {
  std::string id{};
  std::string description{};
  bool required = true;
};

struct VisualChecklistResult {
  std::string id{};
  bool passed = false;
  std::string notes{};
};

struct GoldenArtifactDigest {
  std::string artifact_id{};
  std::string kind{}; // "png" | "structural"
  std::string sha256{};
};

struct GoldenChangeRecordEntry {
  std::string artifact_id{};
  std::string previous_sha256{}; // empty when the artifact is new
  std::string next_sha256{};
  std::string reason{};
  std::string approved_by_role{};
  std::string approved_at_utc{};
};

struct VisualReviewRecord {
  std::string status{}; // "approved" | "rejected"
  std::string approved_by_role{};
  std::string approved_at_utc{};
  std::string golden_manifest_sha256{};
  VisualTolerance tolerances{};
  std::vector<VisualChecklistResult> checklist_results{};
  std::string method{};
};

struct TypographyObservation {
  std::string node_id{};
  std::string text_run_id{};
  std::string text_role{};
  double font_size_px = 0;
  std::uint64_t text_codepoint_count = 0;
};

struct ShotBoundaryObservation {
  std::string shot_id{};
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  std::uint64_t sample_frame = 0;
  ContentSafeRect content_safe{};
  std::vector<TypographyObservation> typography{};
};

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_visual_tolerance(const VisualTolerance &tolerance);

[[nodiscard]] std::expected<ImageCompareStats, diagnostics::Error>
compare_rgba8_images(std::uint32_t width_px, std::uint32_t height_px,
                     std::span<const std::uint8_t> expected_rgba,
                     std::span<const std::uint8_t> actual_rgba,
                     const VisualTolerance &tolerance);

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_checklist_results(std::span<const VisualChecklistItem> checklist,
                           std::span<const VisualChecklistResult> results);

// Every digest that changed between manifests must be covered by an explicit
// change-record entry. Unchanged digests need no entry.
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_golden_change_records(
    std::span<const GoldenArtifactDigest> previous,
    std::span<const GoldenArtifactDigest> next,
    std::span<const GoldenChangeRecordEntry> records);

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_visual_review_record(std::span<const VisualChecklistItem> checklist,
                              const VisualReviewRecord &record);

// Deterministic structural golden text for shot-boundary / safe-area /
// typography review. PNG reference frames are compared separately.
[[nodiscard]] std::string
format_shot_boundary_golden(const ShotBoundaryObservation &observation);

[[nodiscard]] constexpr std::string_view
editorial_visual_acceptance_method() noexcept {
  return "checklist+reference-frames+tolerance; subjective impression "
         "insufficient";
}

} // namespace instantee::direction
