#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/write_all.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace {

using instantee::export_core::DurabilityClass;
using instantee::export_core::OutputTransaction;
using instantee::export_core::OutputTransactionConfig;
using instantee::export_core::PublishMode;
using instantee::export_core::write_all;

// Minimal ISO BMFF-shaped payload so post-publish checks can assert ftyp.
// Explicit bytes — a string_view from a C string would truncate at the first
// NUL.
constexpr std::array<char, 24> kPayload{
    0, 0, 0,    0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm',
    0, 0, 0x02, 0,    'i', 's', 'o', 'm', 'i', 's', 'o', '2'};

std::string g_wait_phase;

void ready() {
  std::puts("READY");
  std::fflush(stdout);
}

void wait_forever() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::hours(24));
  }
}

void phase_barrier(const char *phase) {
  if (g_wait_phase == phase) {
    ready();
    wait_forever();
  }
}

[[nodiscard]] bool write_staging(const std::filesystem::path &path) {
  const int fd =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) {
    return false;
  }
  auto bytes = std::as_bytes(std::span{kPayload});
  auto written = write_all(fd, bytes);
  const int closed = ::close(fd);
  return static_cast<bool>(written) && closed == 0;
}

[[nodiscard]] int run(std::string_view phase,
                      const std::filesystem::path &target) {
  g_wait_phase.assign(phase.begin(), phase.end());

  OutputTransactionConfig config;
  config.target_path = target;
  // Overwrite keeps the previous target intact until rename, so SIGKILL before
  // rename can assert byte-identical preservation of an existing file.
  config.mode = PublishMode::overwrite;
  config.durability = DurabilityClass::process_crash_visible;
  if (phase != "finish") {
    config.faults.phase_barrier = &phase_barrier;
  }

  auto txn = OutputTransaction::begin(config);
  if (!txn || !write_staging(txn->staging_file())) {
    return 20;
  }

  if (phase == "finish") {
    ready();
    wait_forever();
  }

  auto published = txn->commit();
  if (!published || !published->published) {
    return 21;
  }

  // report phase parks inside commit after cleanup. If the harness asks for a
  // post-success park (published), stay alive with the target visible.
  if (phase == "published") {
    ready();
    wait_forever();
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <phase> <target>\n", argv[0]);
    return 2;
  }
  const std::string_view phase = argv[1];
  static constexpr std::string_view kPhases[] = {
      "finish", "file_sync", "rename", "metadata_sync", "report", "published"};
  bool known = false;
  for (const auto candidate : kPhases) {
    if (phase == candidate) {
      known = true;
      break;
    }
  }
  if (!known) {
    return 3;
  }
  return run(phase, std::filesystem::path{argv[2]});
}
