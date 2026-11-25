#include <instantee/export_core/durability_capability.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/write_all.hpp>

#include <cstdlib>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

using instantee::diagnostics::ExitCode;
using instantee::export_core::DurabilityCapability;
using instantee::export_core::DurabilityClass;
using instantee::export_core::OutputTransaction;
using instantee::export_core::OutputTransactionConfig;
using instantee::export_core::probe_durability_capability;
using instantee::export_core::PublishMode;
using instantee::export_core::require_durability_support;
using instantee::export_core::write_all;

[[nodiscard]] int fail(int code) { return code; }

[[nodiscard]] std::filesystem::path make_temp_dir() {
  auto directory =
      std::filesystem::temp_directory_path() / "instantee-durab-XXXXXX";
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

[[nodiscard]] int check_probe_local_volume() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(1);
  }
  auto capability = probe_durability_capability(dir);
  if (!capability || !capability->process_crash_visible ||
      capability->sigkill_is_power_loss_evidence ||
      capability->filesystem_type.empty()) {
    return fail(2);
  }
  // V1 CI runs on local APFS; durable-local must be accepted without demotion.
  if (!capability->durable_local) {
    return fail(3);
  }
  if (!require_durability_support(DurabilityClass::durable_local,
                                  *capability) ||
      !require_durability_support(DurabilityClass::process_crash_visible,
                                  *capability)) {
    return fail(4);
  }

  DurabilityCapability unsupported = *capability;
  unsupported.durable_local = false;
  auto rejected =
      require_durability_support(DurabilityClass::durable_local, unsupported);
  if (rejected || rejected.error().code != "durability_unsupported") {
    return fail(5);
  }
  std::filesystem::remove_all(dir);
  return 0;
}

[[nodiscard]] int check_begin_rejects_forced_unsupported() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(6);
  }
  OutputTransactionConfig config;
  config.target_path = dir / "out.mp4";
  config.mode = PublishMode::no_replace;
  config.durability = DurabilityClass::durable_local;
  config.faults.force_unsupported_filesystem = true;
  auto txn = OutputTransaction::begin(config);
  if (txn || txn.error().code != "durability_unsupported" ||
      txn.error().exit_code != ExitCode::invalid_input) {
    return fail(7);
  }
  if (std::filesystem::exists(config.target_path)) {
    return fail(8);
  }
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().filename().string().starts_with(".instantee-txn-")) {
      return fail(9);
    }
  }
  std::filesystem::remove_all(dir);
  return 0;
}

[[nodiscard]] int check_durable_local_publish_on_supported_volume() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(10);
  }
  OutputTransactionConfig config;
  config.target_path = dir / "durable.mp4";
  config.mode = PublishMode::no_replace;
  config.durability = DurabilityClass::durable_local;
  auto txn = OutputTransaction::begin(config);
  if (!txn || !write_staging_payload(txn->staging_file(), "durable-payload")) {
    return fail(11);
  }
  auto published = txn->commit();
  if (!published || !published->published ||
      published->durability !=
          instantee::export_core::DurabilityState::confirmed) {
    return fail(12);
  }
  std::ifstream input(config.target_path, std::ios::binary);
  const std::string body{std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{}};
  if (body != "durable-payload") {
    return fail(13);
  }
  std::filesystem::remove_all(dir);
  return 0;
}

[[nodiscard]] int check_phase_barrier_order() {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(14);
  }
  static thread_local std::string phases;
  phases.clear();
  OutputTransactionConfig config;
  config.target_path = dir / "barrier.mp4";
  config.durability = DurabilityClass::process_crash_visible;
  config.faults.phase_barrier = [](const char *phase) {
    if (!phases.empty()) {
      phases.push_back(',');
    }
    phases.append(phase);
  };
  auto txn = OutputTransaction::begin(config);
  if (!txn || !write_staging_payload(txn->staging_file(), "ordered")) {
    return fail(15);
  }
  auto published = txn->commit();
  if (!published || phases != "file_sync,rename,metadata_sync,report") {
    return fail(16);
  }
  std::filesystem::remove_all(dir);
  return 0;
}

} // namespace

[[nodiscard]] int write_capability_report(const std::filesystem::path &path) {
  const auto dir = make_temp_dir();
  if (dir.empty()) {
    return fail(20);
  }
  auto capability = probe_durability_capability(dir);
  if (!capability) {
    return fail(21);
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path);
  if (!out) {
    return fail(22);
  }
  out << "{\n"
      << "  \"schema_version\": 1,\n"
      << "  \"subject\": \"local filesystem durability capability\",\n"
      << "  \"filesystem_type\": \"" << capability->filesystem_type << "\",\n"
      << "  \"process_crash_visible\": "
      << (capability->process_crash_visible ? "true" : "false") << ",\n"
      << "  \"durable_local\": "
      << (capability->durable_local ? "true" : "false") << ",\n"
      << "  \"sigkill_is_power_loss_evidence\": false,\n"
      << "  \"durable_local_requirements\": "
      << "\"APFS/HFS+ with F_FULLFSYNC; unsupported mounts are rejected\",\n"
      << "  \"storage_fault_experiment\": "
      << "\"unit faults cover ENOSPC/EIO/short-write/EINTR, pre-publish "
         "file-sync failure, and post-publish directory-sync unknown "
         "durability; absolute power-loss is out of scope\"\n"
      << "}\n";
  std::filesystem::remove_all(dir);
  return out ? 0 : fail(23);
}

int main(int argc, char **argv) {
  if (const int code = check_probe_local_volume()) {
    return code;
  }
  if (const int code = check_begin_rejects_forced_unsupported()) {
    return code;
  }
  if (const int code = check_durable_local_publish_on_supported_volume()) {
    return code;
  }
  if (const int code = check_phase_barrier_order()) {
    return code;
  }
  if (argc > 1) {
    if (const int code = write_capability_report(argv[1])) {
      return code;
    }
  }
  return 0;
}
