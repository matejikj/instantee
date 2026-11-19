#include <instantee/export_core/export_recovery.hpp>

#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string_view>
#include <sys/file.h>
#include <unistd.h>
#include <utility>

namespace instantee::export_core {
namespace {

using diagnostics::Error;
using diagnostics::ErrorKind;
using diagnostics::ExitCode;

constexpr std::string_view kStagingPrefix = ".instantee-txn-";
constexpr const char *kMarkerName = ".instantee-output-txn";
constexpr const char *kPhase = "export_recovery";

[[nodiscard]] Error make_error(ExitCode exit_code, std::string_view code,
                               std::string_view message) {
  return Error{exit_code, Error::kind_for(exit_code), code, kPhase, message};
}

[[nodiscard]] bool is_single_component(const std::filesystem::path &name) {
  const auto text = name.generic_string();
  return !text.empty() && text != "." && text != ".." &&
         text.find('/') == std::string::npos &&
         text.find('\\') == std::string::npos;
}

[[nodiscard]] std::expected<void, Error>
validate_query(const ExportWorkspaceQuery &query) {
  if (query.parent_directory.empty()) {
    return std::unexpected(make_error(ExitCode::invalid_input, "empty_parent",
                                      "Recovery parent directory is empty"));
  }
  if (!is_single_component(query.target_filename)) {
    return std::unexpected(make_error(ExitCode::invalid_input, "invalid_target",
                                      "Target filename must be a single path "
                                      "component"));
  }
  if (query.report_filename && !is_single_component(*query.report_filename)) {
    return std::unexpected(make_error(ExitCode::invalid_input, "invalid_report",
                                      "Report filename must be a single path "
                                      "component"));
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(query.parent_directory, ec) || ec) {
    return std::unexpected(make_error(ExitCode::invalid_input, "parent_missing",
                                      "Recovery parent directory is missing"));
  }
  return {};
}

[[nodiscard]] std::optional<std::string>
staging_nonce(std::string_view directory_name) {
  if (!directory_name.starts_with(kStagingPrefix)) {
    return std::nullopt;
  }
  auto nonce = std::string(directory_name.substr(kStagingPrefix.size()));
  if (nonce.empty()) {
    return std::nullopt;
  }
  return nonce;
}

[[nodiscard]] std::expected<std::string, Error>
read_marker_nonce(const std::filesystem::path &marker_path) {
  std::ifstream input(marker_path, std::ios::binary);
  if (!input) {
    return std::unexpected(make_error(ExitCode::missing_or_corrupt_asset,
                                      "marker_unreadable",
                                      "Cannot read staging ownership marker"));
  }
  std::string nonce;
  nonce.assign(std::istreambuf_iterator<char>{input},
               std::istreambuf_iterator<char>{});
  if (nonce.empty()) {
    return std::unexpected(make_error(ExitCode::missing_or_corrupt_asset,
                                      "marker_empty",
                                      "Staging ownership marker is empty"));
  }
  return nonce;
}

enum class LockProbe : std::uint8_t { active, orphan, unreadable };

[[nodiscard]] LockProbe probe_staging_lock(const std::filesystem::path &dir,
                                           std::string_view expected_nonce) {
  const auto marker_path = dir / kMarkerName;
  const int fd = ::open(marker_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return LockProbe::unreadable;
  }
  auto marker = read_marker_nonce(marker_path);
  if (!marker || *marker != expected_nonce) {
    ::close(fd);
    return LockProbe::unreadable;
  }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    const int err = errno;
    ::close(fd);
    if (err == EWOULDBLOCK || err == EAGAIN) {
      return LockProbe::active;
    }
    return LockProbe::unreadable;
  }
  // Orphan: we hold the exclusive lock; release before returning so recovery
  // can remove the directory without leaving a dangling lock owner.
  ::flock(fd, LOCK_UN);
  ::close(fd);
  return LockProbe::orphan;
}

[[nodiscard]] std::expected<ExportWorkspaceInspection, Error>
inspect_impl(const ExportWorkspaceQuery &query) {
  if (auto valid = validate_query(query); !valid) {
    return std::unexpected(valid.error());
  }

  ExportWorkspaceInspection out;
  const auto target_path = query.parent_directory / query.target_filename;
  std::error_code ec;
  out.published_target_present =
      std::filesystem::is_regular_file(target_path, ec) && !ec;
  if (out.published_target_present) {
    out.observations.push_back(ExportArtifactObservation{
        .kind = ExportArtifactKind::published_target,
        .path = target_path,
    });
  }

  if (query.report_filename) {
    const auto report_path = query.parent_directory / *query.report_filename;
    ec.clear();
    out.report_present =
        std::filesystem::is_regular_file(report_path, ec) && !ec;
    out.observations.push_back(ExportArtifactObservation{
        .kind = out.report_present ? ExportArtifactKind::report_present
                                   : ExportArtifactKind::missing_report,
        .path = report_path,
    });
  }

  for (std::filesystem::directory_iterator it(query.parent_directory, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (ec) {
      break;
    }
    const auto &entry = *it;
    std::error_code dir_ec;
    if (!entry.is_directory(dir_ec) || dir_ec) {
      continue;
    }
    const auto name = entry.path().filename().string();
    auto nonce = staging_nonce(name);
    if (!nonce) {
      continue;
    }
    ExportArtifactObservation obs;
    obs.path = entry.path();
    obs.nonce = *nonce;
    switch (probe_staging_lock(entry.path(), *nonce)) {
    case LockProbe::active:
      obs.kind = ExportArtifactKind::active_staging;
      break;
    case LockProbe::orphan:
      obs.kind = ExportArtifactKind::orphan_staging;
      break;
    case LockProbe::unreadable:
      // Name matches our pattern but marker is wrong/missing: leave alone.
      obs.kind = ExportArtifactKind::foreign_entry;
      break;
    }
    out.observations.push_back(std::move(obs));
  }
  if (ec) {
    return std::unexpected(
        make_error(ExitCode::export_failure, "dir_iterate",
                   "Cannot iterate export parent directory"));
  }
  return out;
}

} // namespace

std::expected<ExportWorkspaceInspection, Error>
inspect_export_workspace(const ExportWorkspaceQuery &query) noexcept {
  try {
    return inspect_impl(query);
  } catch (...) {
    return std::unexpected(make_error(ExitCode::unexpected_exception,
                                      "inspect_exception",
                                      "Unexpected exception during export "
                                      "workspace inspection"));
  }
}

std::expected<ExportRecoverySummary, Error>
recover_orphan_staging(const ExportWorkspaceQuery &query) noexcept {
  try {
    auto inspection = inspect_impl(query);
    if (!inspection) {
      return std::unexpected(inspection.error());
    }
    ExportRecoverySummary summary;
    summary.inspection = std::move(*inspection);
    for (auto &obs : summary.inspection.observations) {
      if (obs.kind == ExportArtifactKind::active_staging) {
        ++summary.active_skipped;
        continue;
      }
      if (obs.kind == ExportArtifactKind::foreign_entry) {
        ++summary.foreign_skipped;
        continue;
      }
      if (obs.kind != ExportArtifactKind::orphan_staging) {
        continue;
      }
      // Re-check lock immediately before delete to avoid racing a new job that
      // reused a directory name (extremely unlikely with random nonces).
      if (probe_staging_lock(obs.path, obs.nonce) != LockProbe::orphan) {
        obs.kind = ExportArtifactKind::active_staging;
        ++summary.active_skipped;
        continue;
      }
      std::error_code remove_ec;
      std::filesystem::remove_all(obs.path, remove_ec);
      if (remove_ec) {
        return std::unexpected(
            make_error(ExitCode::export_failure, "orphan_remove_failed",
                       "Cannot remove orphan staging directory"));
      }
      obs.removed = true;
      ++summary.orphans_removed;
    }
    return summary;
  } catch (...) {
    return std::unexpected(make_error(ExitCode::unexpected_exception,
                                      "recover_exception",
                                      "Unexpected exception during orphan "
                                      "staging recovery"));
  }
}

} // namespace instantee::export_core
