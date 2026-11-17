#include <instantee/render_core/pipeline_cache.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

struct CountingPayload final : instantee::render_core::PipelinePayload {
  explicit CountingPayload(int id_in) : id(id_in) {}
  int id = 0;
};

[[nodiscard]] instantee::render_core::PipelineVariantKey
make_key(std::string_view fingerprint, bool blend = false) {
  return instantee::render_core::PipelineVariantKey{
      .feature_name = "test_pattern",
      .feature_version = 1,
      .pass_key = "main_color",
      .module_id = "test_pattern",
      .module_version = 1,
      .metallib_fingerprint = std::string{fingerprint},
      .color_pixel_format = 70, // stand-in for RGBA8Unorm
      .blending_enabled = blend,
  };
}

} // namespace

int main() {
  using namespace instantee::render_core;
  namespace diagnostics = instantee::diagnostics;

  const std::uint8_t metallib_bytes[] = {0x4d, 0x54, 0x4c, 0x42, 0x01};
  const std::string fp = fingerprint_metallib_bytes(metallib_bytes);
  expect(fp.size() == 16, "fingerprint width");
  expect(fingerprint_metallib_bytes(metallib_bytes) == fp,
         "fingerprint stable");

  PipelineVariantKey key = make_key(fp);
  const std::string canonical = canonicalize_pipeline_key(key);
  expect(!canonical.empty(), "canonical non-empty");
  expect(canonicalize_pipeline_key(key) == canonical, "canonical stable");

  PipelineVariantKey blended = make_key(fp, true);
  expect(canonicalize_pipeline_key(blended) != canonical, "blend changes key");

  PipelineCache cache;
  int compile_calls = 0;
  auto first = cache.get_or_compile(key, [&]() {
    ++compile_calls;
    return std::expected<std::unique_ptr<PipelinePayload>, diagnostics::Error>{
        std::make_unique<CountingPayload>(11)};
  });
  expect(first.has_value(), "first compile ok");
  expect(compile_calls == 1, "compile once on miss");
  expect(cache.stats().misses == 1 && cache.stats().compiles == 1 &&
             cache.stats().hits == 0,
         "miss counters");

  auto second = cache.get_or_compile(key, [&]() {
    ++compile_calls;
    return std::expected<std::unique_ptr<PipelinePayload>, diagnostics::Error>{
        std::make_unique<CountingPayload>(99)};
  });
  expect(second.has_value(), "second lookup ok");
  expect(compile_calls == 1, "repeating shot does not recompile");
  expect(cache.stats().hits == 1 && cache.stats().compiles == 1,
         "hit counters");
  expect(dynamic_cast<CountingPayload *>(*second)->id == 11,
         "same payload reused");

  // Failed compile does not insert a tombstone.
  PipelineVariantKey other = make_key(fp);
  other.pass_key = "shadow";
  auto failed = cache.get_or_compile(other, [&]() {
    ++compile_calls;
    return std::expected<std::unique_ptr<PipelinePayload>, diagnostics::Error>{
        std::unexpected(diagnostics::Error{diagnostics::ExitCode::gpu_failure,
                                           diagnostics::ErrorKind::gpu_failure,
                                           "boom", "test",
                                           "forced compile failure"})};
  });
  expect(!failed.has_value(), "failed compile surfaces");
  expect(cache.stats().misses == 2 && cache.stats().compiles == 2,
         "failed compile counted");
  expect(!cache.contains(other), "no tombstone after failure");
  auto retry = cache.get_or_compile(other, [&]() {
    ++compile_calls;
    return std::expected<std::unique_ptr<PipelinePayload>, diagnostics::Error>{
        std::make_unique<CountingPayload>(22)};
  });
  expect(retry.has_value(), "retry after failure compiles");
  expect(compile_calls == 3, "retry invoked compile");

  auto blob = cache.write_index_blob(fp);
  expect(blob.has_value(), "write index blob");
  auto index = PipelineCache::read_index_blob(*blob, fp);
  expect(index.has_value(), "read index blob");
  if (index) {
    expect(index->format_version == PipelineCache::kFormatVersion,
           "format version");
    expect(index->metallib_fingerprint == fp, "fingerprint roundtrip");
    expect(index->canonical_keys.size() == 2, "two keys indexed");
  }

  // Corruption: bad magic.
  auto bad_magic = *blob;
  bad_magic[0] = 'X';
  auto magic_err = PipelineCache::read_index_blob(bad_magic, fp);
  expect(!magic_err.has_value(), "bad magic rejected");
  if (!magic_err) {
    expect(magic_err.error().code == "pipeline_cache_bad_magic",
           "bad magic code");
    expect(magic_err.error().kind ==
               diagnostics::ErrorKind::missing_or_corrupt_asset,
           "bad magic kind");
  }

  // Truncation.
  auto truncated = std::vector<std::uint8_t>(blob->begin(), blob->begin() + 6);
  auto trunc_err = PipelineCache::read_index_blob(truncated, fp);
  expect(!trunc_err.has_value(), "truncated rejected");
  if (!trunc_err) {
    expect(trunc_err.error().code == "pipeline_cache_truncated",
           "truncated code");
  }

  // Incompatible metallib fingerprint.
  auto mismatch = PipelineCache::read_index_blob(*blob, "deadbeefdeadbeef");
  expect(!mismatch.has_value(), "fingerprint mismatch rejected");
  if (!mismatch) {
    expect(mismatch.error().code == "pipeline_cache_metallib_mismatch",
           "mismatch code");
    expect(mismatch.error().kind == diagnostics::ErrorKind::incompatible_schema,
           "mismatch kind");
  }

  // Incompatible format version.
  auto bad_version = *blob;
  bad_version[4] = 9; // little-endian u32 version field after magic
  auto ver_err = PipelineCache::read_index_blob(bad_version, fp);
  expect(!ver_err.has_value(), "version mismatch rejected");
  if (!ver_err) {
    expect(ver_err.error().code == "pipeline_cache_version_mismatch",
           "version code");
    expect(ver_err.error().kind == diagnostics::ErrorKind::incompatible_schema,
           "version kind");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
