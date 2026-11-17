#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace instantee::render_core {

// Canonical identity for a compiled pipeline variant. Built from feature
// registry fields plus metallib fingerprint and attachment state. GPU handles
// are never part of the key and are never written to disk.
struct PipelineVariantKey {
  std::string feature_name;
  std::uint32_t feature_version = 1;
  std::string pass_key;
  std::string module_id;
  std::uint32_t module_version = 1;
  // Hex fingerprint of the metallib bytes (see fingerprint_metallib_bytes).
  std::string metallib_fingerprint;
  std::uint32_t color_pixel_format = 0;
  bool blending_enabled = false;
};

[[nodiscard]] bool operator==(const PipelineVariantKey &lhs,
                              const PipelineVariantKey &rhs) noexcept;

// Stable, printable form used as the in-memory map key and disk index entry.
[[nodiscard]] std::string
canonicalize_pipeline_key(const PipelineVariantKey &key);

// FNV-1a 64-bit rendered as 16 lowercase hex digits. Stable across platforms.
[[nodiscard]] std::string
fingerprint_metallib_bytes(std::span<const std::uint8_t> bytes);

// Type-erased compiled pipeline. Backends own concrete PSO payloads; the cache
// only manages lifetime and lookup. Destruction runs once on erase/clear.
struct PipelinePayload {
  PipelinePayload() = default;
  PipelinePayload(const PipelinePayload &) = delete;
  PipelinePayload &operator=(const PipelinePayload &) = delete;
  PipelinePayload(PipelinePayload &&) = delete;
  PipelinePayload &operator=(PipelinePayload &&) = delete;
  virtual ~PipelinePayload() = default;
};

struct PipelineCacheStats {
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t compiles = 0;
};

struct PipelineCacheIndex {
  std::uint32_t format_version = 0;
  std::string metallib_fingerprint;
  std::vector<std::string> canonical_keys{};
};

// In-memory reuse of compiled pipelines plus a durable index blob (keys and
// metallib fingerprint only). Corrupted or incompatible blobs fail closed.
class PipelineCache {
public:
  static constexpr std::uint32_t kFormatVersion = 1;

  [[nodiscard]] const PipelineCacheStats &stats() const noexcept {
    return stats_;
  }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool contains(const PipelineVariantKey &key) const;

  [[nodiscard]] PipelinePayload *find(const PipelineVariantKey &key) noexcept;
  [[nodiscard]] const PipelinePayload *
  find(const PipelineVariantKey &key) const noexcept;

  // Inserts a freshly compiled payload. Duplicate keys are rejected.
  [[nodiscard]] std::expected<PipelinePayload *, diagnostics::Error>
  insert(const PipelineVariantKey &key,
         std::unique_ptr<PipelinePayload> payload);

  // Lookup or compile. A cache hit never invokes `compile`. A miss increments
  // misses+compiles and stores the factory result under the canonical key.
  template <class CompileFn>
  [[nodiscard]] std::expected<PipelinePayload *, diagnostics::Error>
  get_or_compile(const PipelineVariantKey &key, CompileFn &&compile) {
    if (PipelinePayload *existing = find(key)) {
      ++stats_.hits;
      return existing;
    }
    ++stats_.misses;
    ++stats_.compiles;
    std::expected<std::unique_ptr<PipelinePayload>, diagnostics::Error> built =
        std::forward<CompileFn>(compile)();
    if (!built) {
      // Failed compile does not leave a tombstone entry.
      return std::unexpected(built.error());
    }
    return insert(key, std::move(*built));
  }

  void clear() noexcept;

  // Serializes only canonical keys + metallib fingerprint. No GPU handles.
  [[nodiscard]] std::expected<std::vector<std::uint8_t>, diagnostics::Error>
  write_index_blob(std::string_view metallib_fingerprint) const;

  // Validates magic/version/fingerprint and returns the key index. Truncation,
  // bad magic, version skew and fingerprint mismatch fail with typed errors.
  [[nodiscard]] static std::expected<PipelineCacheIndex, diagnostics::Error>
  read_index_blob(std::span<const std::uint8_t> bytes,
                  std::string_view expected_metallib_fingerprint);

private:
  PipelineCacheStats stats_{};
  std::unordered_map<std::string, std::unique_ptr<PipelinePayload>> entries_{};
};

} // namespace instantee::render_core
