#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/write_all.hpp>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using instantee::diagnostics::ExitCode;
using instantee::export_core::DurabilityClass;
using instantee::export_core::DurabilityState;
using instantee::export_core::OutputTransaction;
using instantee::export_core::OutputTransactionConfig;
using instantee::export_core::PublishMode;
using instantee::export_core::write_all;

[[nodiscard]] int fail(int code) { return code; }

[[nodiscard]] std::filesystem::path make_temp_dir() {
  auto directory =
      std::filesystem::temp_directory_path() / "instantee-txn-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] std::expected<void, instantee::diagnostics::Error>
write_staging_payload(const std::filesystem::path &path,
                      std::string_view payload) {
  const int fd =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    return std::unexpected(
        instantee::diagnostics::Error{ExitCode::export_failure, "open_failed",
                                      "test", "Cannot create staging payload"});
  }
  auto bytes = std::as_bytes(std::span{payload.data(), payload.size()});
  auto written = write_all(fd, bytes);
  if (::close(fd) != 0 && written) {
    return std::unexpected(
        instantee::diagnostics::Error{ExitCode::export_failure, "close_failed",
                                      "test", "Cannot close staging payload"});
  }
  return written;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string{std::istreambuf_iterator<char>{input},
                     std::istreambuf_iterator<char>{}};
}

[[nodiscard]] int check_write_all_faults() {
  std::vector<std::byte> buffer(8);
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    buffer[i] = static_cast<std::byte>(i + 1U);
  }

  // Short writes that eventually complete.
  std::size_t calls = 0;
  auto short_write = [&](int, const void *,
                         std::size_t size) -> std::ptrdiff_t {
    ++calls;
    return size == 0 ? 0 : 1;
  };
  if (!write_all(3, buffer, short_write) || calls != buffer.size()) {
    return fail(1);
  }

  // EINTR then success.
  bool interrupted = false;
  auto eintr_then_ok = [&](int, const void *,
                           std::size_t size) -> std::ptrdiff_t {
    if (!interrupted) {
      interrupted = true;
      errno = EINTR;
      return -1;
    }
    return static_cast<std::ptrdiff_t>(size);
  };
  if (!write_all(3, buffer, eintr_then_ok) || !interrupted) {
    return fail(2);
  }

  auto enospc = [](int, const void *, std::size_t) -> std::ptrdiff_t {
    errno = ENOSPC;
    return -1;
  };
  auto enospc_result = write_all(3, buffer, enospc);
  if (enospc_result || enospc_result.error().code != "enospc") {
    return fail(3);
  }

  auto eio = [](int, const void *, std::size_t) -> std::ptrdiff_t {
    errno = EIO;
    return -1;
  };
  auto eio_result = write_all(3, buffer, eio);
  if (eio_result || eio_result.error().code != "eio") {
    return fail(4);
  }

  auto zero_progress = [](int, const void *, std::size_t) -> std::ptrdiff_t {
    return 0;
  };
  auto zero_result = write_all(3, buffer, zero_progress);
  if (zero_result || zero_result.error().code != "short_write") {
    return fail(5);
  }
  return 0;
}

[[nodiscard]] int check_destroy_keeps_old_target() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(6);
  }
  const auto target = dir / "final.mp4";
  {
    std::ofstream out(target, std::ios::binary);
    out << "old-target";
  }
  const auto old_contents = read_file(target);

  {
    OutputTransactionConfig config;
    config.target_path = target;
    config.durability = DurabilityClass::process_crash_visible;
    auto txn = OutputTransaction::begin(config);
    if (!txn) {
      return fail(7);
    }
    if (!write_staging_payload(txn->staging_file(), "partial")) {
      return fail(8);
    }
    if (!std::filesystem::exists(txn->staging_directory())) {
      return fail(9);
    }
    // Destructor abandons without publish.
  }

  if (!std::filesystem::exists(target) || read_file(target) != old_contents) {
    return fail(10);
  }
  // Staging directories with our prefix should be gone after destructor
  // cleanup.
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().filename().string().starts_with(".instantee-txn-")) {
      return fail(11);
    }
  }
  std::filesystem::remove_all(dir);
  return 0;
}

[[nodiscard]] int check_successful_no_replace_publish() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(12);
  }
  const auto target = dir / "movie.mp4";
  OutputTransactionConfig config;
  config.target_path = target;
  config.mode = PublishMode::no_replace;
  config.durability = DurabilityClass::durable_local;
  auto txn = OutputTransaction::begin(config);
  if (!txn || std::filesystem::exists(txn->staging_file())) {
    return fail(13);
  }
  if (!write_staging_payload(txn->staging_file(), "complete-mp4")) {
    return fail(14);
  }
  auto published = txn->commit();
  if (!published || !published->published ||
      published->durability != DurabilityState::confirmed ||
      published->post_publish_error || !txn->published()) {
    return fail(15);
  }
  if (!std::filesystem::exists(target) || read_file(target) != "complete-mp4") {
    return fail(16);
  }
  if (std::filesystem::exists(txn->staging_file())) {
    return fail(17);
  }
  std::filesystem::remove_all(dir);
  return 0;
}

[[nodiscard]] int check_existing_target_conflict_and_overwrite() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(18);
  }
  const auto target = dir / "movie.mp4";
  {
    std::ofstream out(target, std::ios::binary);
    out << "existing";
  }

  OutputTransactionConfig no_replace;
  no_replace.target_path = target;
  no_replace.mode = PublishMode::no_replace;
  no_replace.durability = DurabilityClass::process_crash_visible;
  auto blocked = OutputTransaction::begin(no_replace);
  if (!blocked || !write_staging_payload(blocked->staging_file(), "new-a")) {
    return fail(19);
  }
  auto conflict = blocked->commit();
  if (conflict ||
      conflict.error().exit_code != ExitCode::publication_conflict ||
      conflict.error().code != "target_exists" ||
      read_file(target) != "existing") {
    return fail(20);
  }

  OutputTransactionConfig overwrite;
  overwrite.target_path = target;
  overwrite.mode = PublishMode::overwrite;
  overwrite.durability = DurabilityClass::process_crash_visible;
  auto replacing = OutputTransaction::begin(overwrite);
  if (!replacing ||
      !write_staging_payload(replacing->staging_file(), "new-b")) {
    return fail(21);
  }
  auto replaced = replacing->commit();
  if (!replaced || !replaced->published || read_file(target) != "new-b") {
    return fail(22);
  }
  std::filesystem::remove_all(dir);
  return 0;
}

[[nodiscard]] int check_sync_failure_before_and_after_publish() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(23);
  }
  const auto target = dir / "movie.mp4";
  {
    std::ofstream out(target, std::ios::binary);
    out << "keep-me";
  }

  OutputTransactionConfig before;
  before.target_path = target;
  before.mode = PublishMode::overwrite;
  before.durability = DurabilityClass::process_crash_visible;
  before.faults.fail_file_sync_before_publish = true;
  auto txn_before = OutputTransaction::begin(before);
  if (!txn_before || !write_staging_payload(txn_before->staging_file(),
                                            "should-not-publish")) {
    return fail(24);
  }
  auto failed = txn_before->commit();
  if (failed || failed.error().code != "file_sync_failed" ||
      read_file(target) != "keep-me" || txn_before->published()) {
    return fail(25);
  }

  OutputTransactionConfig after;
  after.target_path = target;
  after.mode = PublishMode::overwrite;
  after.durability = DurabilityClass::process_crash_visible;
  after.faults.fail_directory_sync_after_publish = true;
  auto txn_after = OutputTransaction::begin(after);
  if (!txn_after ||
      !write_staging_payload(txn_after->staging_file(), "visible")) {
    return fail(26);
  }
  auto published = txn_after->commit();
  if (!published || !published->published ||
      published->durability != DurabilityState::unknown ||
      !published->post_publish_error ||
      published->post_publish_error->exit_code !=
          ExitCode::published_durability_unknown ||
      read_file(target) != "visible") {
    return fail(27);
  }
  std::filesystem::remove_all(dir);
  return 0;
}

[[nodiscard]] int check_concurrent_no_replace() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(28);
  }
  const auto target = dir / "race.mp4";

  OutputTransactionConfig config_a;
  config_a.target_path = target;
  config_a.mode = PublishMode::no_replace;
  config_a.durability = DurabilityClass::process_crash_visible;
  OutputTransactionConfig config_b = config_a;

  auto txn_a = OutputTransaction::begin(config_a);
  auto txn_b = OutputTransaction::begin(config_b);
  if (!txn_a || !txn_b ||
      !write_staging_payload(txn_a->staging_file(), "winner-a") ||
      !write_staging_payload(txn_b->staging_file(), "winner-b")) {
    return fail(29);
  }

  std::atomic_int successes{0};
  std::atomic_int conflicts{0};
  std::string visible;
  std::mutex visible_mutex;

  std::thread thread_a([&] {
    auto result = txn_a->commit();
    if (result && result->published) {
      successes.fetch_add(1);
      std::lock_guard lock(visible_mutex);
      visible = read_file(target);
    } else if (!result &&
               result.error().exit_code == ExitCode::publication_conflict) {
      conflicts.fetch_add(1);
    }
  });
  std::thread thread_b([&] {
    auto result = txn_b->commit();
    if (result && result->published) {
      successes.fetch_add(1);
      std::lock_guard lock(visible_mutex);
      visible = read_file(target);
    } else if (!result &&
               result.error().exit_code == ExitCode::publication_conflict) {
      conflicts.fetch_add(1);
    }
  });
  thread_a.join();
  thread_b.join();

  if (successes.load() != 1 || conflicts.load() != 1) {
    return fail(30);
  }
  if (visible != "winner-a" && visible != "winner-b") {
    return fail(31);
  }
  std::filesystem::remove_all(dir);
  return 0;
}

} // namespace

int main() {
  if (const int code = check_write_all_faults()) {
    return code;
  }
  if (const int code = check_destroy_keeps_old_target()) {
    return code;
  }
  if (const int code = check_successful_no_replace_publish()) {
    return code;
  }
  if (const int code = check_existing_target_conflict_and_overwrite()) {
    return code;
  }
  if (const int code = check_sync_failure_before_and_after_publish()) {
    return code;
  }
  if (const int code = check_concurrent_no_replace()) {
    return code;
  }
  return 0;
}
