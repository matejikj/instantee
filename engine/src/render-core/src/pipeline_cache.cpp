#include <instantee/render_core/pipeline_cache.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace instantee::render_core {
namespace {

constexpr std::array<char, 4> kMagic{'I', 'P', 'C', '1'};

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "pipeline_cache", message};
}

[[nodiscard]] diagnostics::Error make_corrupt(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::missing_or_corrupt_asset,
                            diagnostics::ErrorKind::missing_or_corrupt_asset,
                            code, "pipeline_cache", message};
}

[[nodiscard]] diagnostics::Error make_incompatible(std::string_view code,
                                                   std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::incompatible_schema,
                            diagnostics::ErrorKind::incompatible_schema, code,
                            "pipeline_cache", message};
}

[[nodiscard]] bool valid_token(std::string_view value) noexcept {
  return !value.empty();
}

[[nodiscard]] constexpr std::uint64_t
fnv1a64_bytes(std::span<const std::uint8_t> bytes) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (std::uint8_t byte : bytes) {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] std::string hex16(std::uint64_t value) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kDigits[value & 0xfu];
    value >>= 4U;
  }
  return out;
}

[[nodiscard]] bool append_u32(std::vector<std::uint8_t> &out,
                              std::uint32_t value) {
  const std::size_t before = out.size();
  if (before >
      (std::numeric_limits<std::size_t>::max)() - sizeof(std::uint32_t)) {
    return false;
  }
  out.resize(before + sizeof(std::uint32_t));
  out[before] = static_cast<std::uint8_t>(value & 0xffu);
  out[before + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
  out[before + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
  out[before + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
  return true;
}

[[nodiscard]] bool append_bytes(std::vector<std::uint8_t> &out,
                                std::span<const std::uint8_t> bytes) {
  if (bytes.size() > (std::numeric_limits<std::size_t>::max)() - out.size()) {
    return false;
  }
  out.insert(out.end(), bytes.begin(), bytes.end());
  return true;
}

[[nodiscard]] std::expected<std::uint32_t, diagnostics::Error>
read_u32(std::span<const std::uint8_t> bytes, std::size_t &offset) {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t)) {
    return std::unexpected(
        make_corrupt("pipeline_cache_truncated",
                     "pipeline cache blob ended inside a u32 field"));
  }
  const std::uint32_t value =
      static_cast<std::uint32_t>(bytes[offset]) |
      (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
      (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
      (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
  offset += sizeof(std::uint32_t);
  return value;
}

[[nodiscard]] std::expected<std::string, diagnostics::Error>
read_string(std::span<const std::uint8_t> bytes, std::size_t &offset) {
  auto length = read_u32(bytes, offset);
  if (!length) {
    return std::unexpected(length.error());
  }
  if (*length > static_cast<std::uint32_t>(
                    (std::numeric_limits<std::uint32_t>::max)() / 4U)) {
    return std::unexpected(
        make_corrupt("pipeline_cache_string_too_large",
                     "pipeline cache string length exceeds safety limit"));
  }
  if (offset > bytes.size() || bytes.size() - offset < *length) {
    return std::unexpected(
        make_corrupt("pipeline_cache_truncated",
                     "pipeline cache blob ended inside a string field"));
  }
  std::string value(reinterpret_cast<const char *>(bytes.data() + offset),
                    *length);
  offset += *length;
  return value;
}

} // namespace

bool operator==(const PipelineVariantKey &lhs,
                const PipelineVariantKey &rhs) noexcept {
  return lhs.feature_name == rhs.feature_name &&
         lhs.feature_version == rhs.feature_version &&
         lhs.pass_key == rhs.pass_key && lhs.module_id == rhs.module_id &&
         lhs.module_version == rhs.module_version &&
         lhs.metallib_fingerprint == rhs.metallib_fingerprint &&
         lhs.color_pixel_format == rhs.color_pixel_format &&
         lhs.blending_enabled == rhs.blending_enabled;
}

std::string canonicalize_pipeline_key(const PipelineVariantKey &key) {
  // Field order is part of the contract; do not reorder without bumping
  // PipelineCache::kFormatVersion and migrating blobs.
  std::string out;
  out.reserve(128);
  out.append("feature=");
  out.append(key.feature_name);
  out.append(";feature_ver=");
  out.append(std::to_string(key.feature_version));
  out.append(";pass=");
  out.append(key.pass_key);
  out.append(";module=");
  out.append(key.module_id);
  out.append(";module_ver=");
  out.append(std::to_string(key.module_version));
  out.append(";metallib=");
  out.append(key.metallib_fingerprint);
  out.append(";format=");
  out.append(std::to_string(key.color_pixel_format));
  out.append(";blend=");
  out.append(key.blending_enabled ? "1" : "0");
  return out;
}

std::string fingerprint_metallib_bytes(std::span<const std::uint8_t> bytes) {
  return hex16(fnv1a64_bytes(bytes));
}

bool PipelineCache::contains(const PipelineVariantKey &key) const {
  return entries_.contains(canonicalize_pipeline_key(key));
}

PipelinePayload *PipelineCache::find(const PipelineVariantKey &key) noexcept {
  const auto it = entries_.find(canonicalize_pipeline_key(key));
  if (it == entries_.end()) {
    return nullptr;
  }
  return it->second.get();
}

const PipelinePayload *
PipelineCache::find(const PipelineVariantKey &key) const noexcept {
  const auto it = entries_.find(canonicalize_pipeline_key(key));
  if (it == entries_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::expected<PipelinePayload *, diagnostics::Error>
PipelineCache::insert(const PipelineVariantKey &key,
                      std::unique_ptr<PipelinePayload> payload) {
  if (!valid_token(key.feature_name) || !valid_token(key.pass_key) ||
      !valid_token(key.module_id) || !valid_token(key.metallib_fingerprint)) {
    return std::unexpected(make_invalid(
        "invalid_pipeline_key",
        "pipeline variant key is missing a required identity field"));
  }
  if (key.feature_version == 0 || key.module_version == 0) {
    return std::unexpected(
        make_invalid("invalid_pipeline_key_version",
                     "pipeline variant feature/module version must be > 0"));
  }
  if (!payload) {
    return std::unexpected(make_invalid(
        "null_pipeline_payload", "pipeline cache refuses a null payload"));
  }
  const std::string canonical = canonicalize_pipeline_key(key);
  const auto [it, inserted] = entries_.emplace(canonical, std::move(payload));
  if (!inserted) {
    return std::unexpected(
        make_invalid("duplicate_pipeline_key",
                     "pipeline cache already holds this canonical key"));
  }
  return it->second.get();
}

void PipelineCache::clear() noexcept {
  entries_.clear();
  stats_ = {};
}

std::expected<std::vector<std::uint8_t>, diagnostics::Error>
PipelineCache::write_index_blob(std::string_view metallib_fingerprint) const {
  if (!valid_token(metallib_fingerprint)) {
    return std::unexpected(make_invalid(
        "invalid_metallib_fingerprint",
        "pipeline cache index requires a non-empty metallib fingerprint"));
  }
  if (metallib_fingerprint.size() >
      static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
    return std::unexpected(
        make_invalid("metallib_fingerprint_too_large",
                     "metallib fingerprint exceeds u32 length encoding"));
  }

  std::vector<std::string> keys;
  keys.reserve(entries_.size());
  for (const auto &[canonical, _] : entries_) {
    keys.push_back(canonical);
  }
  std::sort(keys.begin(), keys.end());

  std::vector<std::uint8_t> out;
  out.reserve(64 + keys.size() * 64);
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  if (!append_u32(out, kFormatVersion)) {
    return std::unexpected(
        make_invalid("pipeline_cache_encode_overflow",
                     "pipeline cache index encoding overflowed"));
  }
  if (!append_u32(out,
                  static_cast<std::uint32_t>(metallib_fingerprint.size())) ||
      !append_bytes(out, std::span<const std::uint8_t>(
                             reinterpret_cast<const std::uint8_t *>(
                                 metallib_fingerprint.data()),
                             metallib_fingerprint.size()))) {
    return std::unexpected(
        make_invalid("pipeline_cache_encode_overflow",
                     "pipeline cache index encoding overflowed"));
  }
  if (keys.size() >
      static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
    return std::unexpected(
        make_invalid("pipeline_cache_too_many_keys",
                     "pipeline cache index entry count exceeds u32"));
  }
  if (!append_u32(out, static_cast<std::uint32_t>(keys.size()))) {
    return std::unexpected(
        make_invalid("pipeline_cache_encode_overflow",
                     "pipeline cache index encoding overflowed"));
  }
  for (const std::string &key : keys) {
    if (key.size() >
        static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
      return std::unexpected(
          make_invalid("pipeline_cache_key_too_large",
                       "canonical pipeline key exceeds u32 length encoding"));
    }
    if (!append_u32(out, static_cast<std::uint32_t>(key.size())) ||
        !append_bytes(out,
                      std::span<const std::uint8_t>(
                          reinterpret_cast<const std::uint8_t *>(key.data()),
                          key.size()))) {
      return std::unexpected(
          make_invalid("pipeline_cache_encode_overflow",
                       "pipeline cache index encoding overflowed"));
    }
  }
  return out;
}

std::expected<PipelineCacheIndex, diagnostics::Error>
PipelineCache::read_index_blob(std::span<const std::uint8_t> bytes,
                               std::string_view expected_metallib_fingerprint) {
  if (!valid_token(expected_metallib_fingerprint)) {
    return std::unexpected(
        make_invalid("invalid_metallib_fingerprint",
                     "expected metallib fingerprint must be non-empty"));
  }
  if (bytes.size() < kMagic.size() + sizeof(std::uint32_t)) {
    return std::unexpected(
        make_corrupt("pipeline_cache_truncated",
                     "pipeline cache blob is shorter than the fixed header"));
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    return std::unexpected(
        make_corrupt("pipeline_cache_bad_magic",
                     "pipeline cache blob magic does not match IPC1"));
  }

  std::size_t offset = kMagic.size();
  auto version = read_u32(bytes, offset);
  if (!version) {
    return std::unexpected(version.error());
  }
  if (*version != kFormatVersion) {
    return std::unexpected(
        make_incompatible("pipeline_cache_version_mismatch",
                          "pipeline cache format version is not supported"));
  }

  auto fingerprint = read_string(bytes, offset);
  if (!fingerprint) {
    return std::unexpected(fingerprint.error());
  }
  if (*fingerprint != expected_metallib_fingerprint) {
    return std::unexpected(
        make_incompatible("pipeline_cache_metallib_mismatch",
                          "pipeline cache metallib fingerprint does not match "
                          "the loaded module"));
  }

  auto count = read_u32(bytes, offset);
  if (!count) {
    return std::unexpected(count.error());
  }
  // Defensive cap: index is metadata only, but refuse absurd sizes.
  constexpr std::uint32_t kMaxKeys = 100000;
  if (*count > kMaxKeys) {
    return std::unexpected(
        make_corrupt("pipeline_cache_too_many_keys",
                     "pipeline cache entry count exceeds safety limit"));
  }

  PipelineCacheIndex index;
  index.format_version = *version;
  index.metallib_fingerprint = std::move(*fingerprint);
  index.canonical_keys.reserve(*count);
  for (std::uint32_t i = 0; i < *count; ++i) {
    auto key = read_string(bytes, offset);
    if (!key) {
      return std::unexpected(key.error());
    }
    if (key->empty()) {
      return std::unexpected(
          make_corrupt("pipeline_cache_empty_key",
                       "pipeline cache index contains an empty canonical key"));
    }
    index.canonical_keys.push_back(std::move(*key));
  }
  if (offset != bytes.size()) {
    return std::unexpected(make_corrupt(
        "pipeline_cache_trailing_bytes",
        "pipeline cache blob has trailing bytes after the key index"));
  }
  return index;
}

} // namespace instantee::render_core
