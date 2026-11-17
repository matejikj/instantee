#include <instantee/direction/editorial_visual_acceptance.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace instantee::direction {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "editorial_visual_acceptance", message};
}

[[nodiscard]] bool is_sha256_hex(std::string_view text) noexcept {
  if (text.size() != 64) {
    return false;
  }
  return std::all_of(text.begin(), text.end(), [](unsigned char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
  });
}

[[nodiscard]] bool non_empty(std::string_view text) noexcept {
  return !text.empty();
}

} // namespace

std::expected<void, diagnostics::Error>
validate_visual_tolerance(const VisualTolerance &tolerance) {
  if (!(tolerance.max_mismatch_ratio >= 0.0) ||
      !(tolerance.max_mismatch_ratio <= 1.0) ||
      !std::isfinite(tolerance.max_mismatch_ratio)) {
    return std::unexpected(
        make_invalid("invalid_mismatch_ratio",
                     "max_mismatch_ratio must be a finite value in [0, 1]"));
  }
  return {};
}

std::expected<ImageCompareStats, diagnostics::Error>
compare_rgba8_images(std::uint32_t width_px, std::uint32_t height_px,
                     std::span<const std::uint8_t> expected_rgba,
                     std::span<const std::uint8_t> actual_rgba,
                     const VisualTolerance &tolerance) {
  if (auto ok = validate_visual_tolerance(tolerance); !ok) {
    return std::unexpected(ok.error());
  }
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(
        make_invalid("invalid_extent", "image width/height must be > 0"));
  }
  const std::uint64_t pixels = static_cast<std::uint64_t>(width_px) *
                               static_cast<std::uint64_t>(height_px);
  const std::size_t expected_bytes = static_cast<std::size_t>(pixels) * 4U;
  if (expected_rgba.size() != expected_bytes ||
      actual_rgba.size() != expected_bytes) {
    return std::unexpected(
        make_invalid("rgba_size_mismatch",
                     "RGBA buffers must contain width*height*4 bytes"));
  }

  ImageCompareStats stats{};
  stats.pixels = pixels;
  for (std::size_t i = 0; i < expected_bytes; i += 4) {
    std::uint8_t pixel_delta = 0;
    for (std::size_t c = 0; c < 4; ++c) {
      const int diff = static_cast<int>(expected_rgba[i + c]) -
                       static_cast<int>(actual_rgba[i + c]);
      const auto abs_diff = static_cast<std::uint8_t>(diff < 0 ? -diff : diff);
      pixel_delta = std::max(pixel_delta, abs_diff);
    }
    stats.max_observed_channel_delta =
        std::max(stats.max_observed_channel_delta, pixel_delta);
    if (pixel_delta > tolerance.max_channel_delta) {
      ++stats.mismatched_pixels;
    }
  }
  const double ratio = static_cast<double>(stats.mismatched_pixels) /
                       static_cast<double>(stats.pixels);
  stats.within_tolerance = ratio <= tolerance.max_mismatch_ratio;
  return stats;
}

std::expected<void, diagnostics::Error>
validate_checklist_results(std::span<const VisualChecklistItem> checklist,
                           std::span<const VisualChecklistResult> results) {
  if (checklist.empty()) {
    return std::unexpected(make_invalid(
        "empty_checklist", "visual acceptance requires a non-empty checklist"));
  }
  std::unordered_map<std::string, const VisualChecklistResult *> by_id;
  by_id.reserve(results.size());
  for (const auto &result : results) {
    if (!non_empty(result.id)) {
      return std::unexpected(
          make_invalid("empty_result_id", "checklist result id is required"));
    }
    if (!by_id.emplace(result.id, &result).second) {
      return std::unexpected(make_invalid(
          "duplicate_result_id", "checklist result ids must be unique"));
    }
  }
  for (const auto &item : checklist) {
    if (!non_empty(item.id) || !non_empty(item.description)) {
      return std::unexpected(
          make_invalid("invalid_checklist_item",
                       "checklist items require id and description"));
    }
    const auto it = by_id.find(item.id);
    if (it == by_id.end()) {
      return std::unexpected(
          make_invalid("missing_checklist_result",
                       "required checklist item has no result: " + item.id));
    }
    if (item.required && !it->second->passed) {
      return std::unexpected(
          make_invalid("required_checklist_failed",
                       "required checklist item failed: " + item.id));
    }
  }
  for (const auto &result : results) {
    const bool known = std::any_of(
        checklist.begin(), checklist.end(),
        [&](const VisualChecklistItem &item) { return item.id == result.id; });
    if (!known) {
      return std::unexpected(make_invalid(
          "unknown_checklist_result",
          "checklist result does not match an item: " + result.id));
    }
  }
  return {};
}

std::expected<void, diagnostics::Error> validate_golden_change_records(
    std::span<const GoldenArtifactDigest> previous,
    std::span<const GoldenArtifactDigest> next,
    std::span<const GoldenChangeRecordEntry> records) {
  std::unordered_map<std::string, std::string> prev_by_id;
  prev_by_id.reserve(previous.size());
  for (const auto &artifact : previous) {
    if (!non_empty(artifact.artifact_id) || !is_sha256_hex(artifact.sha256)) {
      return std::unexpected(
          make_invalid("invalid_previous_digest",
                       "previous golden digests need id and sha256"));
    }
    if (!prev_by_id.emplace(artifact.artifact_id, artifact.sha256).second) {
      return std::unexpected(
          make_invalid("duplicate_previous_artifact",
                       "previous golden artifact ids must be unique"));
    }
  }

  std::unordered_map<std::string, std::string> next_by_id;
  next_by_id.reserve(next.size());
  for (const auto &artifact : next) {
    if (!non_empty(artifact.artifact_id) || !is_sha256_hex(artifact.sha256)) {
      return std::unexpected(make_invalid(
          "invalid_next_digest", "next golden digests need id and sha256"));
    }
    if (!next_by_id.emplace(artifact.artifact_id, artifact.sha256).second) {
      return std::unexpected(
          make_invalid("duplicate_next_artifact",
                       "next golden artifact ids must be unique"));
    }
  }

  std::unordered_set<std::string> covered;
  for (const auto &entry : records) {
    if (!non_empty(entry.artifact_id) || !non_empty(entry.reason) ||
        !non_empty(entry.approved_by_role) ||
        !non_empty(entry.approved_at_utc) ||
        !is_sha256_hex(entry.next_sha256)) {
      return std::unexpected(make_invalid("invalid_change_record",
                                          "change records require artifact, "
                                          "reason, role, UTC time, next hash"));
    }
    if (!entry.previous_sha256.empty() &&
        !is_sha256_hex(entry.previous_sha256)) {
      return std::unexpected(make_invalid(
          "invalid_change_record_previous",
          "previous_sha256 must be empty or 64 lowercase hex digits"));
    }
    const auto next_it = next_by_id.find(entry.artifact_id);
    if (next_it == next_by_id.end() || next_it->second != entry.next_sha256) {
      return std::unexpected(make_invalid(
          "change_record_next_mismatch",
          "change record next_sha256 must match the next manifest"));
    }
    const auto prev_it = prev_by_id.find(entry.artifact_id);
    const std::string expected_prev =
        prev_it == prev_by_id.end() ? std::string{} : prev_it->second;
    if (entry.previous_sha256 != expected_prev) {
      return std::unexpected(make_invalid(
          "change_record_previous_mismatch",
          "change record previous_sha256 must match the previous manifest"));
    }
    if (entry.previous_sha256 == entry.next_sha256) {
      return std::unexpected(
          make_invalid("change_record_noop",
                       "change records must describe an actual digest change"));
    }
    covered.insert(entry.artifact_id);
  }

  std::unordered_set<std::string> ids;
  for (const auto &artifact : previous) {
    ids.insert(artifact.artifact_id);
  }
  for (const auto &artifact : next) {
    ids.insert(artifact.artifact_id);
  }
  for (const auto &id : ids) {
    const auto prev_it = prev_by_id.find(id);
    const auto next_it = next_by_id.find(id);
    const std::string prev_hash =
        prev_it == prev_by_id.end() ? std::string{} : prev_it->second;
    const std::string next_hash =
        next_it == next_by_id.end() ? std::string{} : next_it->second;
    if (prev_hash == next_hash) {
      continue;
    }
    if (!covered.contains(id)) {
      return std::unexpected(make_invalid(
          "missing_change_record",
          "golden digest change requires an explicit change record: " + id));
    }
  }
  return {};
}

std::expected<void, diagnostics::Error>
validate_visual_review_record(std::span<const VisualChecklistItem> checklist,
                              const VisualReviewRecord &record) {
  if (record.status != "approved") {
    return std::unexpected(make_invalid(
        "review_not_approved", "visual review record status must be approved"));
  }
  if (!non_empty(record.approved_by_role) ||
      !non_empty(record.approved_at_utc) ||
      !is_sha256_hex(record.golden_manifest_sha256)) {
    return std::unexpected(make_invalid(
        "invalid_review_identity",
        "approved review needs role, UTC time, and golden manifest sha256"));
  }
  if (record.method != editorial_visual_acceptance_method()) {
    return std::unexpected(make_invalid(
        "invalid_review_method",
        "review method must require checklist, frames, and tolerance"));
  }
  if (auto ok = validate_visual_tolerance(record.tolerances); !ok) {
    return std::unexpected(ok.error());
  }
  return validate_checklist_results(checklist, record.checklist_results);
}

std::string
format_shot_boundary_golden(const ShotBoundaryObservation &observation) {
  std::ostringstream out;
  out << "shot=" << observation.shot_id << " start=" << observation.start_frame
      << " duration=" << observation.duration_frames
      << " sample=" << observation.sample_frame << '\n';
  out << "safe x=" << observation.content_safe.x_px
      << " y=" << observation.content_safe.y_px
      << " w=" << observation.content_safe.width_px
      << " h=" << observation.content_safe.height_px << '\n';
  out << "typography_count=" << observation.typography.size() << '\n';
  for (const auto &text : observation.typography) {
    out << "text node=" << text.node_id << " run=" << text.text_run_id
        << " role=" << text.text_role << " size=" << text.font_size_px
        << " cps=" << text.text_codepoint_count << '\n';
  }
  return out.str();
}

} // namespace instantee::direction
