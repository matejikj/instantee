#include <instantee/render_core/pipeline_cache.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/test_pattern.hpp>

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::render_metal;
  using namespace instantee::render_core;

  auto device = Device::open();
  expect(device.has_value(), "open Metal device");
  if (!device) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  PipelineCache cache;
  auto first = TestPatternRenderer::create(*device, &cache);
  expect(first.has_value(), "first renderer create");
  expect(cache.stats().compiles == 1 && cache.stats().misses == 1 &&
             cache.stats().hits == 0,
         "first create compiles once");

  auto second = TestPatternRenderer::create(*device, &cache);
  expect(second.has_value(), "second renderer create");
  expect(cache.stats().compiles == 1 && cache.stats().hits == 1,
         "repeated shot reuses pipeline");

  if (second) {
    auto frame = second->render(0);
    expect(frame.has_value(), "render after cache hit");
    if (frame) {
      expect(frame->rgba.size() == static_cast<std::size_t>(kTestPatternWidth) *
                                       kTestPatternHeight *
                                       kTestPatternChannels,
             "frame byte count");
    }
  }

  // Durability index rejects garbage without crashing.
  std::vector<std::uint8_t> junk{0x00, 0x01, 0x02, 0x03,
                                 0x04, 0x05, 0x06, 0x07};
  auto bad = PipelineCache::read_index_blob(junk, "0123456789abcdef");
  expect(!bad.has_value(), "short junk blob rejected");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
