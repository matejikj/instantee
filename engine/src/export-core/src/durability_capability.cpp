#include <instantee/export_core/durability_capability.hpp>

#include <fcntl.h>
#include <string_view>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/mount.h>
#endif

namespace instantee::export_core {
namespace {

constexpr const char *kPhase = "durability_capability";

[[nodiscard]] diagnostics::Error make_error(diagnostics::ExitCode exit_code,
                                            const char *code,
                                            const char *message) {
  return diagnostics::Error{exit_code, code, kPhase, message};
}

} // namespace

std::expected<DurabilityCapability, diagnostics::Error>
probe_durability_capability(const std::filesystem::path &parent_directory) {
  if (parent_directory.empty()) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "empty_parent",
                                      "Durability probe parent path is empty"));
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(parent_directory, ec) || ec) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "parent_not_directory",
                                      "Durability probe requires an existing "
                                      "directory"));
  }

  const int dir_fd =
      ::open(parent_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir_fd < 0) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "open_parent_failed",
                                      "Cannot open directory for durability "
                                      "probe"));
  }

  DurabilityCapability capability;
  capability.sigkill_is_power_loss_evidence = false;
  // Local POSIX rename + fsync gives process-crash visibility on Darwin local
  // volumes used by V1. Absolute media durability is a separate class.
  capability.process_crash_visible = true;

#if defined(__APPLE__)
  struct statfs fs_info{};
  if (::fstatfs(dir_fd, &fs_info) != 0) {
    ::close(dir_fd);
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "statfs_failed",
                                      "Cannot inspect filesystem for "
                                      "durability probe"));
  }
  ::close(dir_fd);
  capability.filesystem_type = fs_info.f_fstypename;
  const std::string_view fs_type = capability.filesystem_type;
  // F_FULLFSYNC is the durable-local contract on APFS/HFS+; other mounts must
  // be refused rather than silently falling back to fsync.
  capability.durable_local = (fs_type == "apfs" || fs_type == "hfs");
#else
  ::close(dir_fd);
  capability.filesystem_type = "unsupported";
  capability.durable_local = false;
  capability.process_crash_visible = false;
#endif

  return capability;
}

std::expected<void, diagnostics::Error>
require_durability_support(DurabilityClass requested,
                           const DurabilityCapability &capability) {
  if (requested == DurabilityClass::process_crash_visible) {
    if (!capability.process_crash_visible) {
      return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                        "durability_unsupported",
                                        "process-crash-visible is unsupported "
                                        "on this filesystem"));
    }
    return {};
  }

  if (requested == DurabilityClass::durable_local) {
    if (!capability.durable_local) {
      return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                        "durability_unsupported",
                                        "durable-local requires APFS/HFS+ with "
                                        "F_FULLFSYNC"));
    }
    return {};
  }

  return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                    "durability_unsupported",
                                    "Requested durability class is unknown"));
}

} // namespace instantee::export_core
