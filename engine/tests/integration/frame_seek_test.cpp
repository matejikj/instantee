#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

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

int run_capture(const std::string &command, std::string &stdout_text) {
  stdout_text.clear();
  const std::string redirected = command + " 2>/dev/null";
  FILE *pipe = popen(redirected.c_str(), "r");
  if (pipe == nullptr) {
    return -1;
  }
  char buffer[512];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    stdout_text.append(buffer);
  }
  const int status = pclose(pipe);
  if (status == -1) {
    return -1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
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

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: frame_seek_test <source_root> <instantee_bin>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path bin = argv[2];
  const auto scene =
      root / "contracts/fixtures/scene-ir/positive/minimal.scene.json";
  const auto report = std::filesystem::temp_directory_path() /
                      "instantee-frame-seek-report.json";

  std::string stdout_text;
  const std::string seek_cmd = "\"" + bin.string() + "\" frame \"" +
                               scene.string() + "\" --at 450 --out \"" +
                               report.string() + "\"";
  expect(run_capture(seek_cmd, stdout_text) == 0, "frame --at 450 exit 0");
  expect(contains(stdout_text, "\"command\":\"frame\""), "stdout command");
  expect(contains(stdout_text, "\"at\":450"), "stdout at 450");
  expect(contains(stdout_text, "\"evaluations\":1"), "stdout evaluations 1");
  expect(contains(stdout_text, "\"duration_frames\":1800"), "stdout duration");
  expect(std::filesystem::exists(report), "report written");
  if (std::filesystem::exists(report)) {
    std::ifstream input(report);
    std::ostringstream body;
    body << input.rdbuf();
    expect(contains(body.str(), "\"at\":450"), "report at 450");
    expect(contains(body.str(), "\"evaluations\":1"), "report evaluations 1");
  }

  // Same mid-film seek again; still a single evaluation path (no rewind).
  expect(run_capture("\"" + bin.string() + "\" frame \"" + scene.string() +
                         "\" --at 450",
                     stdout_text) == 0,
         "repeat seek exit 0");
  expect(contains(stdout_text, "\"evaluations\":1"), "repeat evaluations 1");

  expect(run_capture("\"" + bin.string() + "\" frame \"" + scene.string() +
                         "\" --at 0",
                     stdout_text) == 0,
         "seek first frame");
  expect(contains(stdout_text, "\"at\":0"), "first frame at 0");

  expect(run_capture("\"" + bin.string() + "\" frame \"" + scene.string() +
                         "\" --at 1799",
                     stdout_text) == 0,
         "seek last frame");
  expect(contains(stdout_text, "\"at\":1799"), "last frame at 1799");

  expect(run_exit("\"" + bin.string() + "\" frame \"" + scene.string() +
                  "\" --at 1800 >/dev/null 2>/dev/null") == 2,
         "out of range exit 2");
  expect(run_exit("\"" + bin.string() + "\" frame \"" + scene.string() +
                  "\" >/dev/null 2>/dev/null") == 2,
         "missing --at exit 2");
  const auto png_path =
      std::filesystem::temp_directory_path() / "instantee-frame-seek-450.png";
  std::error_code ec;
  std::filesystem::remove(png_path, ec);
  expect(run_capture("\"" + bin.string() + "\" frame \"" + scene.string() +
                         "\" --at 450 --out \"" + png_path.string() + "\"",
                     stdout_text) == 0,
         "frame --at 450 png exit 0");
  expect(contains(stdout_text, "\"evaluations\":1"), "png seek evaluations 1");
  expect(contains(stdout_text, "\"out_kind\":\"png\""), "png out kind");
  expect(contains(stdout_text, "\"color_space\":\"rec709-sdr\""),
         "png color space");
  expect(std::filesystem::exists(png_path), "png written");
  if (std::filesystem::exists(png_path)) {
    std::ifstream input(png_path, std::ios::binary);
    std::array<unsigned char, 8> sig{};
    input.read(reinterpret_cast<char *>(sig.data()),
               static_cast<std::streamsize>(sig.size()));
    expect(input.gcount() == 8 && sig[0] == 137 && sig[1] == 80 &&
               sig[2] == 78 && sig[3] == 71,
           "png signature");
    std::filesystem::remove(png_path, ec);
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
