#include <instantee/asset_pipeline/asset_cache.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

namespace instantee::asset_pipeline {
namespace {

constexpr std::array<char, 4> kMagic{'I', 'A', 'C', '1'};
constexpr std::uint32_t kKindBitmap = 1;

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "asset_cache", message};
}

[[nodiscard]] diagnostics::Error make_corrupt(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::missing_or_corrupt_asset,
                            diagnostics::ErrorKind::missing_or_corrupt_asset,
                            code, "asset_cache", message};
}

[[nodiscard]] diagnostics::Error make_incompatible(std::string_view code,
                                                   std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::incompatible_schema,
                            diagnostics::ErrorKind::incompatible_schema, code,
                            "asset_cache", message};
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
    return std::unexpected(make_corrupt(
        "asset_cache_truncated", "asset cache blob ended inside a u32 field"));
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
  if (*length >
      static_cast<std::uint32_t>((std::numeric_limits<std::size_t>::max)())) {
    return std::unexpected(
        make_corrupt("asset_cache_truncated",
                     "asset cache string length exceeds addressable size"));
  }
  if (bytes.size() - offset < *length) {
    return std::unexpected(make_corrupt(
        "asset_cache_truncated", "asset cache blob ended inside a string"));
  }
  std::string out(reinterpret_cast<const char *>(bytes.data() + offset),
                  *length);
  offset += *length;
  return out;
}

[[nodiscard]] std::expected<std::vector<std::uint8_t>, diagnostics::Error>
read_byte_block(std::span<const std::uint8_t> bytes, std::size_t &offset,
                std::size_t count) {
  if (bytes.size() - offset < count) {
    return std::unexpected(make_corrupt(
        "asset_cache_truncated", "asset cache blob ended inside a byte block"));
  }
  std::vector<std::uint8_t> out(
      bytes.begin() + static_cast<std::ptrdiff_t>(offset),
      bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
  offset += count;
  return out;
}

[[nodiscard]] std::string
sha256_hex_bytes(std::span<const std::uint8_t> bytes) {
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
  static constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.resize(digest.size() * 2U);
  for (std::size_t i = 0; i < digest.size(); ++i) {
    hex[i * 2U] = kHex[digest[i] >> 4U];
    hex[i * 2U + 1U] = kHex[digest[i] & 0x0fU];
  }
  return hex;
}

[[nodiscard]] bool encode_flags(std::vector<std::uint8_t> &out,
                                const AssetCompileFlags &flags) {
  return append_u32(out, flags.cache_format_version) &&
         append_u32(out, static_cast<std::uint32_t>(flags.alpha_mode)) &&
         append_u32(out, static_cast<std::uint32_t>(flags.color_policy));
}

[[nodiscard]] bool hash_referenced_by_other(
    const std::unordered_map<std::string, std::string> &id_to_hash,
    std::string_view asset_id, std::string_view content_hash) {
  for (const auto &[id, hash] : id_to_hash) {
    if (id != asset_id && hash == content_hash) {
      return true;
    }
  }
  return false;
}

} // namespace

std::expected<std::string, diagnostics::Error>
asset_content_sha256(std::span<const std::uint8_t> source_bytes,
                     const AssetCompileFlags &flags) {
  // Domain separator + flags + raw source. Flags first so small config changes
  // do not require scanning the full source to notice a key difference in tests
  // that inspect only a prefix; the digest still covers every byte.
  std::vector<std::uint8_t> material;
  material.reserve(16 + source_bytes.size());
  const char prefix[] = {'A', 'S', 'S', 'E', 'T', '1'};
  if (!append_bytes(material,
                    std::span<const std::uint8_t>(
                        reinterpret_cast<const std::uint8_t *>(prefix),
                        sizeof(prefix))) ||
      !encode_flags(material, flags) || !append_bytes(material, source_bytes)) {
    return std::unexpected(
        make_invalid("asset_hash_overflow",
                     "asset content hash materialization overflowed"));
  }
  return sha256_hex_bytes(material);
}

bool AssetDiskCache::contains(std::string_view content_hash) const {
  return entries_.find(std::string(content_hash)) != entries_.end();
}

const NormalizedBitmapRecord *
AssetDiskCache::find(std::string_view content_hash) const noexcept {
  const auto it = entries_.find(std::string(content_hash));
  if (it == entries_.end()) {
    ++stats_.disk_misses;
    return nullptr;
  }
  ++stats_.disk_hits;
  return &it->second;
}

std::expected<const NormalizedBitmapRecord *, diagnostics::Error>
AssetDiskCache::store(NormalizedBitmapRecord record) {
  if (record.content_hash.empty()) {
    return std::unexpected(make_invalid("asset_cache_empty_hash",
                                        "content hash must be non-empty"));
  }
  if (record.bitmap.rgba.size() !=
      static_cast<std::size_t>(record.bitmap.width_px) *
          static_cast<std::size_t>(record.bitmap.height_px) * 4U) {
    return std::unexpected(make_invalid(
        "asset_cache_rgba_size",
        "normalized bitmap rgba size does not match width*height*4"));
  }
  auto [it, inserted] =
      entries_.insert_or_assign(record.content_hash, std::move(record));
  (void)inserted;
  return &it->second;
}

void AssetDiskCache::erase(std::string_view content_hash) noexcept {
  entries_.erase(std::string(content_hash));
}

void AssetDiskCache::clear() noexcept {
  entries_.clear();
  stats_ = {};
}

std::expected<std::vector<std::uint8_t>, diagnostics::Error>
AssetDiskCache::write_blob(const NormalizedBitmapRecord &record) {
  if (record.content_hash.empty()) {
    return std::unexpected(make_invalid("asset_cache_empty_hash",
                                        "content hash must be non-empty"));
  }
  if (record.flags.cache_format_version != kFormatVersion) {
    return std::unexpected(make_incompatible(
        "asset_cache_version_mismatch",
        "cannot write asset cache blob with unsupported format version"));
  }
  const std::size_t px = static_cast<std::size_t>(record.bitmap.width_px) *
                         static_cast<std::size_t>(record.bitmap.height_px);
  if (record.bitmap.rgba.size() != px * 4U) {
    return std::unexpected(make_invalid(
        "asset_cache_rgba_size",
        "normalized bitmap rgba size does not match width*height*4"));
  }
  if (px > (std::numeric_limits<std::uint32_t>::max)() / 4U) {
    return std::unexpected(
        make_invalid("asset_cache_too_large",
                     "normalized bitmap exceeds u32 byte encoding"));
  }

  std::vector<std::uint8_t> out;
  out.reserve(64 + record.content_hash.size() + record.bitmap.rgba.size());
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  if (!append_u32(out, kFormatVersion) || !append_u32(out, kKindBitmap) ||
      !encode_flags(out, record.flags) ||
      !append_u32(out,
                  static_cast<std::uint32_t>(record.content_hash.size())) ||
      !append_bytes(out, std::span<const std::uint8_t>(
                             reinterpret_cast<const std::uint8_t *>(
                                 record.content_hash.data()),
                             record.content_hash.size())) ||
      !append_u32(out, static_cast<std::uint32_t>(record.bitmap.format)) ||
      !append_u32(out, record.bitmap.width_px) ||
      !append_u32(out, record.bitmap.height_px) ||
      !append_u32(out, static_cast<std::uint32_t>(record.bitmap.alpha_mode)) ||
      !append_u32(out,
                  static_cast<std::uint32_t>(record.bitmap.color_policy)) ||
      !append_u32(out, static_cast<std::uint32_t>(record.bitmap.rgba.size())) ||
      !append_bytes(out, record.bitmap.rgba)) {
    return std::unexpected(make_invalid(
        "asset_cache_encode_overflow", "asset cache blob encoding overflowed"));
  }
  return out;
}

std::expected<NormalizedBitmapRecord, diagnostics::Error>
AssetDiskCache::read_blob(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kMagic.size() + sizeof(std::uint32_t)) {
    return std::unexpected(
        make_corrupt("asset_cache_truncated",
                     "asset cache blob is shorter than the fixed header"));
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    return std::unexpected(make_corrupt(
        "asset_cache_bad_magic", "asset cache blob magic does not match IAC1"));
  }

  std::size_t offset = kMagic.size();
  auto version = read_u32(bytes, offset);
  if (!version) {
    return std::unexpected(version.error());
  }
  if (*version != kFormatVersion) {
    return std::unexpected(
        make_incompatible("asset_cache_version_mismatch",
                          "asset cache format version is not supported"));
  }

  auto kind = read_u32(bytes, offset);
  if (!kind) {
    return std::unexpected(kind.error());
  }
  if (*kind != kKindBitmap) {
    return std::unexpected(
        make_incompatible("asset_cache_unknown_kind",
                          "asset cache blob kind is not a normalized bitmap"));
  }

  AssetCompileFlags flags{};
  auto flag_version = read_u32(bytes, offset);
  auto alpha = read_u32(bytes, offset);
  auto color = read_u32(bytes, offset);
  if (!flag_version || !alpha || !color) {
    return std::unexpected(flag_version
                               ? (alpha ? color.error() : alpha.error())
                               : flag_version.error());
  }
  flags.cache_format_version = *flag_version;
  flags.alpha_mode = static_cast<AlphaMode>(*alpha);
  flags.color_policy = static_cast<ColorImportPolicy>(*color);

  auto hash = read_string(bytes, offset);
  if (!hash) {
    return std::unexpected(hash.error());
  }
  if (hash->empty() || hash->size() != 64) {
    return std::unexpected(make_corrupt(
        "asset_cache_bad_hash",
        "asset cache content hash must be 64 lowercase hex digits"));
  }

  auto format = read_u32(bytes, offset);
  auto width = read_u32(bytes, offset);
  auto height = read_u32(bytes, offset);
  auto bmp_alpha = read_u32(bytes, offset);
  auto bmp_color = read_u32(bytes, offset);
  auto rgba_len = read_u32(bytes, offset);
  if (!format || !width || !height || !bmp_alpha || !bmp_color || !rgba_len) {
    return std::unexpected(make_corrupt(
        "asset_cache_truncated", "asset cache blob ended in bitmap header"));
  }

  const std::uint64_t expected_px =
      static_cast<std::uint64_t>(*width) * static_cast<std::uint64_t>(*height);
  if (expected_px > (std::numeric_limits<std::uint64_t>::max)() / 4U) {
    return std::unexpected(
        make_corrupt("asset_cache_rgba_overflow",
                     "asset cache bitmap dimensions overflow byte count"));
  }
  if (*rgba_len != expected_px * 4U) {
    return std::unexpected(
        make_corrupt("asset_cache_rgba_size",
                     "asset cache rgba length does not match width*height*4"));
  }

  auto rgba = read_byte_block(bytes, offset, *rgba_len);
  if (!rgba) {
    return std::unexpected(rgba.error());
  }
  if (offset != bytes.size()) {
    return std::unexpected(make_corrupt(
        "asset_cache_trailing_bytes",
        "asset cache blob has trailing bytes after the bitmap payload"));
  }

  NormalizedBitmapRecord record;
  record.content_hash = std::move(*hash);
  record.flags = flags;
  record.bitmap.format = static_cast<BitmapFormat>(*format);
  record.bitmap.width_px = *width;
  record.bitmap.height_px = *height;
  record.bitmap.alpha_mode = static_cast<AlphaMode>(*bmp_alpha);
  record.bitmap.color_policy = static_cast<ColorImportPolicy>(*bmp_color);
  record.bitmap.rgba = std::move(*rgba);
  return record;
}

bool AssetGpuCache::contains(std::string_view content_hash) const {
  return slots_.find(std::string(content_hash)) != slots_.end();
}

const GpuAssetSlot *
AssetGpuCache::find(std::string_view content_hash) const noexcept {
  const auto it = slots_.find(std::string(content_hash));
  if (it == slots_.end()) {
    return nullptr;
  }
  return &it->second;
}

GpuAssetSlot &AssetGpuCache::bind(std::string_view content_hash) {
  const std::string key(content_hash);
  auto it = slots_.find(key);
  if (it != slots_.end()) {
    ++stats_.gpu_hits;
    return it->second;
  }
  ++stats_.gpu_misses;
  GpuAssetSlot slot;
  slot.content_hash = key;
  slot.generation = next_generation_++;
  auto [ins, _] = slots_.emplace(key, std::move(slot));
  return ins->second;
}

void AssetGpuCache::erase(std::string_view content_hash) noexcept {
  slots_.erase(std::string(content_hash));
}

void AssetGpuCache::clear() noexcept {
  slots_.clear();
  stats_ = {};
  next_generation_ = 1;
}

std::optional<std::string>
AssetCache::content_hash_for(std::string_view asset_id) const {
  const auto it = id_to_hash_.find(std::string(asset_id));
  if (it == id_to_hash_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::expected<std::string, diagnostics::Error>
AssetCache::put_bitmap(std::string_view asset_id,
                       std::span<const std::uint8_t> source_bytes,
                       const AssetCompileFlags &flags, DecodedBitmap bitmap) {
  if (asset_id.empty()) {
    return std::unexpected(
        make_invalid("asset_cache_empty_id", "asset id must be non-empty"));
  }
  auto hash = asset_content_sha256(source_bytes, flags);
  if (!hash) {
    return std::unexpected(hash.error());
  }

  const std::string id(asset_id);
  const auto previous = id_to_hash_.find(id);
  if (previous != id_to_hash_.end() && previous->second != *hash) {
    // Selective invalidation: drop the previous GPU binding only when no other
    // asset id still references that content hash.
    const std::string old_hash = previous->second;
    if (!hash_referenced_by_other(id_to_hash_, id, old_hash)) {
      gpu_.erase(old_hash);
    }
    ++stats_.selective_invalidations;
    ++gpu_.stats_.selective_invalidations;
  }

  NormalizedBitmapRecord record;
  record.content_hash = *hash;
  record.flags = flags;
  record.bitmap = std::move(bitmap);
  auto stored = disk_.store(std::move(record));
  if (!stored) {
    return std::unexpected(stored.error());
  }
  id_to_hash_[id] = *hash;
  return *hash;
}

const NormalizedBitmapRecord *
AssetCache::find_disk_for_asset(std::string_view asset_id) const noexcept {
  const auto it = id_to_hash_.find(std::string(asset_id));
  if (it == id_to_hash_.end()) {
    return nullptr;
  }
  return disk_.find(it->second);
}

GpuAssetSlot *
AssetCache::bind_gpu_for_asset(std::string_view asset_id) noexcept {
  const auto it = id_to_hash_.find(std::string(asset_id));
  if (it == id_to_hash_.end()) {
    return nullptr;
  }
  return &gpu_.bind(it->second);
}

void AssetCache::invalidate_asset(std::string_view asset_id) noexcept {
  const auto it = id_to_hash_.find(std::string(asset_id));
  if (it == id_to_hash_.end()) {
    return;
  }
  const std::string hash = it->second;
  const std::string id = it->first;
  id_to_hash_.erase(it);
  if (!hash_referenced_by_other(id_to_hash_, id, hash)) {
    gpu_.erase(hash);
  }
  ++stats_.selective_invalidations;
  ++gpu_.stats_.selective_invalidations;
}

void AssetCache::clear() noexcept {
  disk_.clear();
  gpu_.clear();
  id_to_hash_.clear();
  stats_ = {};
}

} // namespace instantee::asset_pipeline
