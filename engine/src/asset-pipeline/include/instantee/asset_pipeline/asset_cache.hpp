#pragma once

#include <instantee/asset_pipeline/bitmap_decode.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::asset_pipeline {

// Compile-time identity inputs for a normalized raster asset. Changing any
// field yields a different content hash (SAS §11.2).
struct AssetCompileFlags {
  // Must match AssetDiskCache::kFormatVersion for durable blobs written today.
  std::uint32_t cache_format_version = 1;
  AlphaMode alpha_mode = AlphaMode::straight;
  ColorImportPolicy color_policy = ColorImportPolicy::assume_srgb_ignore_icc;
};

// SHA-256 hex (64 lowercase digits) over source bytes + canonical flags.
// Stable across platforms; does not include asset id (identity is content).
[[nodiscard]] std::expected<std::string, diagnostics::Error>
asset_content_sha256(std::span<const std::uint8_t> source_bytes,
                     const AssetCompileFlags &flags);

struct AssetCacheStats {
  std::uint64_t disk_hits = 0;
  std::uint64_t disk_misses = 0;
  std::uint64_t gpu_hits = 0;
  std::uint64_t gpu_misses = 0;
  std::uint64_t selective_invalidations = 0;
};

// Process-local GPU slot. Never written to disk; backends bind real resources
// elsewhere. A generation bump means the previous GPU binding is stale.
struct GpuAssetSlot {
  std::string content_hash{};
  std::uint64_t generation = 0;
};

// Portable normalized bitmap record (CPU only). No GPU handles.
struct NormalizedBitmapRecord {
  std::string content_hash{};
  AssetCompileFlags flags{};
  DecodedBitmap bitmap{};
};

// Durable, versioned blob codec for normalized bitmaps. Corrupt magic,
// truncation, and format-version skew fail closed with typed errors so the
// caller can rebuild from source.
class AssetDiskCache {
public:
  static constexpr std::uint32_t kFormatVersion = 1;

  [[nodiscard]] const AssetCacheStats &stats() const noexcept { return stats_; }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  [[nodiscard]] bool contains(std::string_view content_hash) const;

  [[nodiscard]] const NormalizedBitmapRecord *
  find(std::string_view content_hash) const noexcept;

  // Insert or replace by content hash. Returns pointer into the map.
  [[nodiscard]] std::expected<const NormalizedBitmapRecord *,
                              diagnostics::Error>
  store(NormalizedBitmapRecord record);

  void erase(std::string_view content_hash) noexcept;
  void clear() noexcept;

  [[nodiscard]] static std::expected<std::vector<std::uint8_t>,
                                     diagnostics::Error>
  write_blob(const NormalizedBitmapRecord &record);

  [[nodiscard]] static std::expected<NormalizedBitmapRecord, diagnostics::Error>
  read_blob(std::span<const std::uint8_t> bytes);

private:
  mutable AssetCacheStats stats_{};
  std::unordered_map<std::string, NormalizedBitmapRecord> entries_{};
};

// Process/device-local GPU identity cache. Serializing slots is forbidden.
class AssetGpuCache {
public:
  [[nodiscard]] const AssetCacheStats &stats() const noexcept { return stats_; }
  [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }

  [[nodiscard]] bool contains(std::string_view content_hash) const;

  [[nodiscard]] const GpuAssetSlot *
  find(std::string_view content_hash) const noexcept;

  // Creates a slot on miss; bumps generation if rebinding the same hash.
  [[nodiscard]] GpuAssetSlot &bind(std::string_view content_hash);

  void erase(std::string_view content_hash) noexcept;
  void clear() noexcept;

private:
  friend class AssetCache;
  AssetCacheStats stats_{};
  std::uint64_t next_generation_ = 1;
  std::unordered_map<std::string, GpuAssetSlot> slots_{};
};

// Coordinates asset-id → content-hash with separate disk and GPU layers.
// Replacing an asset id with new bytes invalidates only that id's previous
// GPU binding; unrelated assets stay cached.
class AssetCache {
public:
  [[nodiscard]] AssetDiskCache &disk() noexcept { return disk_; }
  [[nodiscard]] const AssetDiskCache &disk() const noexcept { return disk_; }
  [[nodiscard]] AssetGpuCache &gpu() noexcept { return gpu_; }
  [[nodiscard]] const AssetGpuCache &gpu() const noexcept { return gpu_; }

  [[nodiscard]] const AssetCacheStats &stats() const noexcept { return stats_; }

  [[nodiscard]] std::optional<std::string>
  content_hash_for(std::string_view asset_id) const;

  // Decode flags + source bytes define the content hash. Stores the normalized
  // bitmap on the disk layer, updates the id→hash map, and drops the previous
  // GPU slot for this id when the hash changes.
  [[nodiscard]] std::expected<std::string, diagnostics::Error>
  put_bitmap(std::string_view asset_id,
             std::span<const std::uint8_t> source_bytes,
             const AssetCompileFlags &flags, DecodedBitmap bitmap);

  [[nodiscard]] const NormalizedBitmapRecord *
  find_disk_for_asset(std::string_view asset_id) const noexcept;

  [[nodiscard]] GpuAssetSlot *
  bind_gpu_for_asset(std::string_view asset_id) noexcept;

  // Drop GPU binding for this asset id only. Disk entries remain so a rebuild
  // can reuse an unchanged content hash.
  void invalidate_asset(std::string_view asset_id) noexcept;

  void clear() noexcept;

private:
  AssetDiskCache disk_{};
  AssetGpuCache gpu_{};
  AssetCacheStats stats_{};
  std::unordered_map<std::string, std::string> id_to_hash_{};
};

} // namespace instantee::asset_pipeline
