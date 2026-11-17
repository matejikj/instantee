#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#error "POSIX wait status decoding required"
#else
#include <sys/wait.h>
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

int run_exit(const std::string &command) {
  const int status = std::system(command.c_str());
  if (status == -1) {
    return -1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: p3_gate_metrics_smoke_test <p3-gate-metrics-bin>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path bin = argv[1];
  const auto out =
      std::filesystem::temp_directory_path() / "instantee-p3-gate-smoke";
  std::error_code ec;
  std::filesystem::remove_all(out, ec);
  std::filesystem::create_directories(out, ec);

  const std::string cmd =
      "\"" + bin.string() + "\" \"" + out.string() + "\" 24 on";
  expect(run_exit(cmd) == 0, "p3-gate-metrics 24-frame exit 0");

  const auto child = out / "child.json";
  expect(std::filesystem::exists(child), "child.json written");
  expect(std::filesystem::exists(out / "output.mp4"), "mp4 published");
  if (std::filesystem::exists(child)) {
    std::ifstream input(child);
    std::ostringstream body;
    body << input.rdbuf();
    const std::string text = body.str();
    expect(text.find("\"verified\":24") != std::string::npos, "verified 24");
    expect(text.find("\"in_flight_peak\":2") != std::string::npos, "K peak");
    expect(text.find("\"encoder_pool_peak\":8") != std::string::npos, "E peak");
    expect(text.find("\"registry_retire_stress_cycles\":256") !=
               std::string::npos,
           "retire stress");
    expect(text.find("P3-geometry-text-mask-60s-v1") != std::string::npos,
           "workload id");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
