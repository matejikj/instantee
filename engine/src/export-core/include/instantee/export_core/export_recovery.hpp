#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace instantee::export_core {

// Classification of entries beside an export target. Recovery only deletes
// orphan_staging directories that carry this process family's ownership marker
// and are not locked by a live OutputTransaction.
enum class ExportArtifactKind : std::uint8_t {
  orphan_staging = 1,
  active_staging,
  foreign_entry,
  published_target,
  missing_report,
  report_present,
};

struct ExportArtifactObservation {
  ExportArtifactKind kind = ExportArtifactKind::foreign_entry;
  std::filesystem::path path{};
  // Nonce from `.instantee-txn-<nonce>` when kind is *staging.
  std::string nonce{};
  // True when recover_orphan_staging removed this orphan directory.
  bool removed = false;
};

struct ExportWorkspaceInspection {
  std::vector<ExportArtifactObservation> observations{};
  bool published_target_present = false;
  bool report_present = false;
};

struct ExportRecoverySummary {
  ExportWorkspaceInspection inspection{};
  std::uint32_t orphans_removed = 0;
  std::uint32_t active_skipped = 0;
  std::uint32_t foreign_skipped = 0;
};

struct ExportWorkspaceQuery {
  // Directory that contains the published target (and staging siblings).
  std::filesystem::path parent_directory{};
  // Single path component, e.g. "movie.mp4".
  std::filesystem::path target_filename{};
  // Optional single path component for the side-car report.
  std::optional<std::filesystem::path> report_filename{};
};

// Inspect without mutating. Never throws; IO failures become Error values.
[[nodiscard]] std::expected<ExportWorkspaceInspection, diagnostics::Error>
inspect_export_workspace(const ExportWorkspaceQuery &query) noexcept;

// Removes only orphan staging directories owned by this framework (marker +
// matching nonce + exclusive lock available). Never deletes the published
// target, report, foreign entries, or staging still locked by a live job.
[[nodiscard]] std::expected<ExportRecoverySummary, diagnostics::Error>
recover_orphan_staging(const ExportWorkspaceQuery &query) noexcept;

} // namespace instantee::export_core
