#include <instantee/export_core/output_transaction.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#if defined(__APPLE__)
#include <sys/mount.h>
#include <sys/stdio.h>
#endif

namespace instantee::export_core {
namespace {

constexpr const char *kPhase = "output_transaction";
constexpr const char *kMarkerName = ".instantee-output-txn";
constexpr const char *kStagingFileName = "output.mp4";

[[nodiscard]] diagnostics::Error make_error(diagnostics::ExitCode exit_code,
                                            const char *code,
                                            const char *message) {
  return diagnostics::Error{exit_code, code, kPhase, message};
}

[[nodiscard]] bool is_single_path_component(const std::filesystem::path &name) {
  const auto text = name.generic_string();
  return !text.empty() && text != "." && text != ".." &&
         text.find('/') == std::string::npos &&
         text.find('\\') == std::string::npos;
}

[[nodiscard]] std::string make_nonce() {
  std::array<std::uint8_t, 8> bytes{};
  std::random_device device;
  for (auto &byte : bytes) {
    byte = static_cast<std::uint8_t>(device());
  }
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(bytes.size() * 2U);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    out[i * 2U] = kHex[(bytes[i] >> 4) & 0x0f];
    out[i * 2U + 1U] = kHex[bytes[i] & 0x0f];
  }
  return out;
}

int close_once(int fd) noexcept {
  if (fd < 0) {
    return 0;
  }
  // close() is not safely retried after EINTR on Darwin; the descriptor may
  // already be released.
  return ::close(fd);
}

class UniqueFd {
public:
  UniqueFd() = default;
  explicit UniqueFd(int fd) noexcept : fd_(fd) {}
  ~UniqueFd() { reset(); }

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  UniqueFd(UniqueFd &&other) noexcept : fd_(other.release()) {}
  UniqueFd &operator=(UniqueFd &&other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

  int release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }

  void reset(int fd = -1) noexcept {
    if (fd_ >= 0) {
      close_once(fd_);
    }
    fd_ = fd;
  }

private:
  int fd_ = -1;
};

[[nodiscard]] std::expected<void, diagnostics::Error>
sync_file(int fd, DurabilityClass durability, bool force_fail) {
  if (force_fail) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "file_sync_failed",
                                      "Injected file sync failure"));
  }
#if defined(__APPLE__)
  if (durability == DurabilityClass::durable_local) {
    if (::fcntl(fd, F_FULLFSYNC) != 0) {
      return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                        "fullfsync_failed",
                                        "F_FULLFSYNC failed for durable-local "
                                        "publish"));
    }
    return {};
  }
#endif
  if (::fsync(fd) != 0) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "fsync_failed",
                                      "fsync failed before publish"));
  }
  return {};
}

[[nodiscard]] std::expected<void, diagnostics::Error>
sync_directory(int dir_fd, bool force_fail) {
  if (force_fail) {
    return std::unexpected(make_error(
        diagnostics::ExitCode::published_durability_unknown, "dir_sync_failed",
        "Injected directory sync failure after publish"));
  }
  if (::fsync(dir_fd) != 0) {
    return std::unexpected(
        make_error(diagnostics::ExitCode::published_durability_unknown,
                   "dir_sync_failed", "Directory sync failed after publish"));
  }
  return {};
}

void remove_tree_best_effort(const std::filesystem::path &path) noexcept {
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

} // namespace

struct OutputTransaction::State {
  OutputTransactionConfig config{};
  std::filesystem::path parent_path{};
  std::filesystem::path staging_directory{};
  std::filesystem::path staging_file{};
  std::string job_nonce{};
  // Exclusive flock on the ownership marker for the live job lifetime so
  // orphan recovery can distinguish active staging from crash leftovers.
  int marker_lock_fd = -1;
  bool published = false;
  bool abandoned = false;
};

OutputTransaction::OutputTransaction(std::unique_ptr<State> state) noexcept
    : state_(std::move(state)) {}

OutputTransaction::OutputTransaction(OutputTransaction &&) noexcept = default;
OutputTransaction &
OutputTransaction::operator=(OutputTransaction &&) noexcept = default;

OutputTransaction::~OutputTransaction() {
  if (!state_) {
    return;
  }
  if (state_->marker_lock_fd >= 0) {
    ::flock(state_->marker_lock_fd, LOCK_UN);
    close_once(std::exchange(state_->marker_lock_fd, -1));
  }
  if (state_->published || state_->abandoned) {
    return;
  }
  state_->abandoned = true;
  remove_tree_best_effort(state_->staging_directory);
}

std::expected<OutputTransaction, diagnostics::Error>
OutputTransaction::begin(OutputTransactionConfig config) {
  if (config.target_path.empty()) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "empty_target",
                                      "Output target path is empty"));
  }
  if (!config.target_path.has_filename() ||
      !is_single_path_component(config.target_path.filename())) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "invalid_target_name",
                                      "Output target must be a single path "
                                      "component under an existing directory"));
  }

  std::error_code ec;
  const auto parent = config.target_path.parent_path().empty()
                          ? std::filesystem::current_path(ec)
                          : config.target_path.parent_path();
  if (ec) {
    return std::unexpected(
        make_error(diagnostics::ExitCode::invalid_input, "parent_unresolved",
                   "Cannot resolve output parent directory"));
  }

  const auto canonical_parent = std::filesystem::weakly_canonical(parent, ec);
  if (ec || !std::filesystem::is_directory(canonical_parent, ec)) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "parent_not_directory",
                                      "Output parent must be an existing "
                                      "directory"));
  }

  // Refuse publishing through a symlinked parent so staging stays on the same
  // concrete directory the caller resolved.
  std::filesystem::path lexically_normal_parent = parent.lexically_normal();
  if (std::filesystem::is_symlink(lexically_normal_parent, ec)) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "parent_is_symlink",
                                      "Output parent directory must not be a "
                                      "symlink"));
  }

  UniqueFd parent_fd{::open(canonical_parent.c_str(), O_RDONLY | O_DIRECTORY)};
  if (!parent_fd) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "open_parent_failed",
                                      "Cannot open output parent directory"));
  }

#if defined(__APPLE__)
  if (config.durability == DurabilityClass::durable_local) {
    struct statfs fs_info{};
    if (::fstatfs(parent_fd.get(), &fs_info) != 0) {
      return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                        "statfs_failed",
                                        "Cannot inspect filesystem for "
                                        "durable-local publish"));
    }
    // APFS and HFS+ expose F_FULLFSYNC; refuse exotic mounts instead of
    // silently weakening the requested durability class.
    const std::string_view fs_type = fs_info.f_fstypename;
    if (fs_type != "apfs" && fs_type != "hfs") {
      return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                        "durability_unsupported",
                                        "durable-local requires APFS/HFS+"));
    }
  }
#endif

  std::string nonce;
  std::filesystem::path staging_directory;
  for (int attempt = 0; attempt < 32; ++attempt) {
    nonce = make_nonce();
    const std::string dir_name = ".instantee-txn-" + nonce;
    if (::mkdirat(parent_fd.get(), dir_name.c_str(), 0700) == 0) {
      staging_directory = canonical_parent / dir_name;
      break;
    }
    if (errno != EEXIST) {
      return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                        "staging_mkdir_failed",
                                        "Cannot create private staging "
                                        "directory"));
    }
  }
  if (staging_directory.empty()) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "staging_mkdir_exhausted",
                                      "Cannot allocate a unique staging "
                                      "directory"));
  }

  UniqueFd staging_fd{
      ::open(staging_directory.c_str(), O_RDONLY | O_DIRECTORY)};
  if (!staging_fd) {
    remove_tree_best_effort(staging_directory);
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "open_staging_failed",
                                      "Cannot open staging directory"));
  }

  UniqueFd marker{::openat(staging_fd.get(), kMarkerName,
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
  if (!marker) {
    remove_tree_best_effort(staging_directory);
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "marker_create_failed",
                                      "Cannot create staging ownership "
                                      "marker"));
  }
  const auto marker_bytes =
      std::as_bytes(std::span{nonce.data(), nonce.size()});
  // Inline minimal write without depending on write_all include cycle.
  {
    std::size_t offset = 0;
    while (offset < marker_bytes.size()) {
      const ssize_t wrote = ::write(marker.get(), marker_bytes.data() + offset,
                                    marker_bytes.size() - offset);
      if (wrote < 0) {
        if (errno == EINTR) {
          continue;
        }
        close_once(marker.release());
        remove_tree_best_effort(staging_directory);
        return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                          "marker_write_failed",
                                          "Cannot write staging marker"));
      }
      if (wrote == 0) {
        close_once(marker.release());
        remove_tree_best_effort(staging_directory);
        return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                          "marker_write_failed",
                                          "Cannot write staging marker"));
      }
      offset += static_cast<std::size_t>(wrote);
    }
  }
  if (::flock(marker.get(), LOCK_EX) != 0) {
    close_once(marker.release());
    remove_tree_best_effort(staging_directory);
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "marker_lock_failed",
                                      "Cannot lock staging ownership marker"));
  }

  auto state = std::make_unique<State>();
  state->config = std::move(config);
  state->config.target_path =
      canonical_parent / state->config.target_path.filename();
  state->parent_path = canonical_parent;
  state->staging_directory = std::move(staging_directory);
  state->staging_file = state->staging_directory / kStagingFileName;
  state->job_nonce = std::move(nonce);
  state->marker_lock_fd = marker.release();
  return OutputTransaction{std::move(state)};
}

const std::filesystem::path &OutputTransaction::target_path() const noexcept {
  static const std::filesystem::path kEmpty;
  return state_ ? state_->config.target_path : kEmpty;
}

const std::filesystem::path &
OutputTransaction::staging_directory() const noexcept {
  static const std::filesystem::path kEmpty;
  return state_ ? state_->staging_directory : kEmpty;
}

const std::filesystem::path &OutputTransaction::staging_file() const noexcept {
  static const std::filesystem::path kEmpty;
  return state_ ? state_->staging_file : kEmpty;
}

bool OutputTransaction::published() const noexcept {
  return state_ && state_->published;
}

const std::string &OutputTransaction::job_nonce() const noexcept {
  static const std::string kEmpty;
  return state_ ? state_->job_nonce : kEmpty;
}

std::expected<PublishResult, diagnostics::Error>
OutputTransaction::commit(std::stop_token stop) {
  if (!state_) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "transaction_not_ready",
                                      "Output transaction is not ready"));
  }
  if (state_->published) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "already_published",
                                      "Output transaction already published"));
  }
  if (state_->abandoned) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "transaction_abandoned",
                                      "Output transaction was abandoned"));
  }

  if (stop.stop_requested()) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "cancelled",
                                      "Publication was cancelled"));
  }

  std::error_code ec;
  if (!std::filesystem::is_regular_file(state_->staging_file, ec)) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "staging_missing",
                                      "Staging output file is missing"));
  }

  UniqueFd file_fd{::open(state_->staging_file.c_str(), O_RDONLY | O_CLOEXEC)};
  if (!file_fd) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "open_staging_file_failed",
                                      "Cannot open staging output for sync"));
  }
  if (auto synced =
          sync_file(file_fd.get(), state_->config.durability,
                    state_->config.faults.fail_file_sync_before_publish);
      !synced) {
    return std::unexpected(synced.error());
  }
  file_fd.reset();

  UniqueFd parent_fd{
      ::open(state_->parent_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  if (!parent_fd) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "open_parent_failed",
                                      "Cannot open parent directory for "
                                      "publish"));
  }

  const std::string to_rel = state_->config.target_path.filename().string();

#if defined(__APPLE__)
  unsigned int flags = 0;
  if (state_->config.mode == PublishMode::no_replace) {
    flags = RENAME_EXCL;
  }
  UniqueFd staging_dir_fd{::open(state_->staging_directory.c_str(),
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  if (!staging_dir_fd) {
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "open_staging_failed",
                                      "Cannot open staging directory for "
                                      "publish"));
  }
  // This stop observation is the serial owner's entry gate to Publishing.
  // No stop checks follow it: rename/sync must return a truthful result.
  if (stop.stop_requested()) {
    return std::unexpected(make_error(diagnostics::ExitCode::invalid_input,
                                      "cancelled",
                                      "Publication was cancelled"));
  }
  if (::renameatx_np(staging_dir_fd.get(), kStagingFileName, parent_fd.get(),
                     to_rel.c_str(), flags) != 0) {
    if (state_->config.mode == PublishMode::no_replace && errno == EEXIST) {
      return std::unexpected(make_error(
          diagnostics::ExitCode::publication_conflict, "target_exists",
          "Exclusive publish found an existing "
          "target"));
    }
    return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                      "rename_failed",
                                      "Atomic publish rename failed"));
  }
#else
  return std::unexpected(make_error(diagnostics::ExitCode::export_failure,
                                    "rename_unsupported",
                                    "Atomic exclusive rename requires macOS"));
#endif

  // Rename is the publication linearization point.
  state_->published = true;

  PublishResult result;
  result.published = true;
  result.durability = DurabilityState::confirmed;

  if (auto dir_synced = sync_directory(
          parent_fd.get(),
          state_->config.faults.fail_directory_sync_after_publish);
      !dir_synced) {
    result.durability = DurabilityState::unknown;
    result.post_publish_error = dir_synced.error();
  }

  // Staging directory may still hold the marker; never delete the published
  // target while cleaning leftovers. Drop the live lock before removal so
  // concurrent recovery does not observe a locked orphan.
  if (state_->marker_lock_fd >= 0) {
    ::flock(state_->marker_lock_fd, LOCK_UN);
    close_once(std::exchange(state_->marker_lock_fd, -1));
  }
  remove_tree_best_effort(state_->staging_directory);
  return result;
}

} // namespace instantee::export_core
