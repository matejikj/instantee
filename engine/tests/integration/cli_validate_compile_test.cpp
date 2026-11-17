#include <cstdlib>
#include <filesystem>
#include <iostream>
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
  if (argc != 3) {
    std::cerr
        << "usage: cli_validate_compile_test <source_root> <instantee_bin>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path bin = argv[2];
  const auto authoring =
      root / "contracts/fixtures/authoring/positive/minimal.authoring.json";
  const auto out = std::filesystem::temp_directory_path() /
                   "instantee-cli-compile.scene.json";

  expect(run_exit("\"" + bin.string() + "\" validate \"" + authoring.string() +
                  "\" >/dev/null") == 0,
         "validate exit 0");

  expect(run_exit("\"" + bin.string() + "\" compile \"" + authoring.string() +
                  "\" --out \"" + out.string() + "\" >/dev/null") == 0,
         "compile exit 0");
  expect(std::filesystem::exists(out), "scene file written");

  const auto bad =
      root /
      "contracts/fixtures/authoring/negative/unknown-intent.authoring.json";
  expect(run_exit("\"" + bin.string() + "\" validate \"" + bad.string() +
                  "\" >/dev/null 2>/dev/null") == 2,
         "unsupported intent exit 2");

  const auto schema =
      root /
      "contracts/fixtures/authoring/negative/unsupported-schema.authoring.json";
  expect(run_exit("\"" + bin.string() + "\" validate \"" + schema.string() +
                  "\" >/dev/null 2>/dev/null") == 4,
         "incompatible schema exit 4");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
