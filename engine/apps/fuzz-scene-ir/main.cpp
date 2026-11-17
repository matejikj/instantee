#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

// Bounded fuzz harness skeleton. Reads stdin (or a file path) up to the
// documented byte limit and exercises parse + structural + graph validation.
// Extended corpus/driver wiring lands with later hardening work.
namespace {

int run_once(const std::uint8_t *data, std::size_t size) {
  instantee::scene_ir::LoadOptions options;
  if (size > options.limits.max_bytes) {
    size = options.limits.max_bytes;
  }
  const auto *bytes = reinterpret_cast<const std::byte *>(data);
  const auto loaded = instantee::scene_ir::load_scene_json(
      std::span<const std::byte>(bytes, size), options);
  if (!loaded) {
    return 0;
  }
  (void)instantee::scene_ir::validate_graph(*loaded);
  return 0;
}

} // namespace

#if defined(INSTANTEE_FUZZ_STANDALONE)
int main(int argc, char **argv) {
  FILE *input = stdin;
  if (argc > 1) {
    input = std::fopen(argv[1], "rb");
    if (input == nullptr) {
      return 2;
    }
  }
  std::vector<std::uint8_t> buffer(
      instantee::scene_ir::InputLimits{}.max_bytes);
  const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), input);
  if (input != stdin) {
    std::fclose(input);
  }
  return run_once(buffer.data(), read);
}
#else
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  return run_once(data, size);
}
#endif
