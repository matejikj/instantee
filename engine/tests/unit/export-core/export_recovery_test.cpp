#include <instantee/export_core/export_recovery.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/write_all.hpp>

#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

using instantee::diagnostics::ExitCode;
using instantee::export_core::DurabilityClass;
using instantee::export_core::ExportArtifactKind;
using instantee::export_core::ExportWorkspaceQuery;
using instantee::export_core::inspect_export_workspace;
using instantee::export_core::OutputTransaction;
using instantee::export_core::OutputTransactionConfig;
using instantee::export_core::PublishMode;
using instantee::export_core::recover_orphan_staging;
using instantee::export_core::write_all;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path make_temp_dir() {
  auto directory =
      std::filesystem::temp_directory_path() / "instantee-recovery-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] bool write_file(const std::filesystem::path &path,
                              std::string_view body) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] bool write_staging_payload(const std::filesystem::path &path,
                                         std::string_view payload) {
  const int fd =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    return false;
  }
  auto bytes = std::as_bytes(std::span{payload.data(), payload.size()});
  auto written = write_all(fd, bytes);
  const int closed = ::close(fd);
  return written.has_value() && closed == 0;
}

void check_classify_published_and_missing_report() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return;
  }
  expect(write_file(dir / "movie.mp4", "published-bytes"), "write mp4");

  ExportWorkspaceQuery query;
  query.parent_directory = dir;
  query.target_filename = "movie.mp4";
  query.report_filename = "movie.report.json";

  auto inspected = inspect_export_workspace(query);
  expect(inspected.has_value(), "inspect ok");
  if (!inspected) {
    std::filesystem::remove_all(dir);
    return;
  }
  expect(inspected->published_target_present, "published present");
  expect(!inspected->report_present, "report missing");
  bool saw_missing_report = false;
  bool saw_published = false;
  for (const auto &obs : inspected->observations) {
    if (obs.kind == ExportArtifactKind::missing_report) {
      saw_missing_report = true;
    }
    if (obs.kind == ExportArtifactKind::published_target) {
      saw_published = true;
    }
  }
  expect(saw_published, "published observation");
  expect(saw_missing_report, "missing report observation");

  expect(write_file(dir / "movie.report.json", "{\"ok\":true}"),
         "write report");
  auto again = inspect_export_workspace(query);
  expect(again.has_value() && again->report_present, "report present");
  std::filesystem::remove_all(dir);
}

void check_orphan_cleanup_and_active_skip() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return;
  }

  // Live transaction holds the marker lock → active_staging.
  OutputTransactionConfig live_config;
  live_config.target_path = dir / "live.mp4";
  live_config.mode = PublishMode::no_replace;
  live_config.durability = DurabilityClass::process_crash_visible;
  auto live_begun = OutputTransaction::begin(live_config);
  expect(live_begun.has_value(), "begin live txn");
  if (!live_begun) {
    std::filesystem::remove_all(dir);
    return;
  }
  std::optional<OutputTransaction> live{std::move(*live_begun)};
  expect(write_staging_payload(live->staging_file(), "in-flight"),
         "live staging payload");

  // Simulated crash leftover: marker + nonce match, but no live flock holder.
  const std::string nonce = "deadbeefcafebabe";
  const auto manual_orphan = dir / (".instantee-txn-" + nonce);
  std::filesystem::create_directory(manual_orphan);
  expect(write_file(manual_orphan / ".instantee-output-txn", nonce),
         "manual marker");
  expect(write_file(manual_orphan / "output.mp4", "leftover"),
         "manual payload");

  // Foreign lookalike without a valid ownership marker must be skipped.
  const auto foreign = dir / ".instantee-txn-notours";
  std::filesystem::create_directory(foreign);
  expect(write_file(foreign / "readme.txt", "do-not-delete"), "foreign file");

  ExportWorkspaceQuery query;
  query.parent_directory = dir;
  query.target_filename = "live.mp4";
  query.report_filename = "live.report.json";

  auto recovered = recover_orphan_staging(query);
  expect(recovered.has_value(), "recover ok");
  if (!recovered) {
    live.reset();
    std::filesystem::remove_all(dir);
    return;
  }
  expect(recovered->orphans_removed >= 1, "removed at least one orphan");
  expect(!std::filesystem::exists(manual_orphan), "manual orphan removed");
  expect(std::filesystem::exists(live->staging_directory()),
         "active staging kept");
  expect(std::filesystem::exists(foreign), "foreign entry kept");
  expect(recovered->active_skipped >= 1, "active skipped");
  expect(recovered->foreign_skipped >= 1, "foreign skipped");

  // Published target must never be deleted by recovery.
  expect(write_file(dir / "live.mp4", "keep-me"), "seed published");
  auto again = recover_orphan_staging(query);
  expect(again.has_value(), "second recover");
  expect(std::filesystem::exists(dir / "live.mp4"), "published preserved");

  live.reset();
  std::filesystem::remove_all(dir);
}

void check_expected_failures_do_not_throw() {
  ExportWorkspaceQuery bad;
  bad.parent_directory = "/definitely/missing/instantee-recovery-parent";
  bad.target_filename = "x.mp4";
  auto inspected = inspect_export_workspace(bad);
  expect(!inspected.has_value(), "missing parent fails");
  expect(inspected.error().exit_code == ExitCode::invalid_input, "exit code");

  ExportWorkspaceQuery bad_name;
  bad_name.parent_directory = std::filesystem::temp_directory_path();
  bad_name.target_filename = "nested/not-single";
  auto recovered = recover_orphan_staging(bad_name);
  expect(!recovered.has_value(), "bad filename fails");
  expect(recovered.error().code == "invalid_target", "invalid_target code");
}

void check_live_txn_destructor_cleans_without_exception() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return;
  }
  OutputTransactionConfig config;
  config.target_path = dir / "cancel.mp4";
  config.durability = DurabilityClass::process_crash_visible;
  std::filesystem::path staging;
  {
    auto begun = OutputTransaction::begin(config);
    expect(begun.has_value(), "begin");
    if (!begun) {
      std::filesystem::remove_all(dir);
      return;
    }
    OutputTransaction txn = std::move(*begun);
    expect(write_staging_payload(txn.staging_file(), "tmp"), "payload");
    staging = txn.staging_directory();
    // Fallible commit path: stop before publish leaves staging for destructor.
    std::stop_source stop;
    stop.request_stop();
    auto published = txn.commit(stop.get_token());
    expect(!published.has_value(), "cancelled commit");
    expect(!std::filesystem::exists(config.target_path), "no target");
  }
  expect(!std::filesystem::exists(staging), "staging cleaned");
  std::filesystem::remove_all(dir);
}

} // namespace

int main() {
  check_classify_published_and_missing_report();
  check_orphan_cleanup_and_active_skip();
  check_expected_failures_do_not_throw();
  check_live_txn_destructor_cleans_without_exception();

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
