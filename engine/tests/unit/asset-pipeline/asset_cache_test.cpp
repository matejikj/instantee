#include <instantee/asset_pipeline/asset_cache.hpp>
#include <instantee/asset_pipeline/bitmap_decode.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#if defined(NANOSVG_H) || defined(STBI_VERSION) || defined(WEBP_WEBP_H_)
#error "asset-pipeline public tests must not pull private decoder headers"
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

instantee::asset_pipeline::DecodedBitmap
make_bitmap(std::uint32_t w, std::uint32_t h, std::uint8_t fill) {
  using namespace instantee::asset_pipeline;
  DecodedBitmap bmp;
  bmp.format = BitmapFormat::png;
  bmp.width_px = w;
  bmp.height_px = h;
  bmp.alpha_mode = AlphaMode::straight;
  bmp.color_policy = ColorImportPolicy::assume_srgb_ignore_icc;
  bmp.rgba.assign(static_cast<std::size_t>(w) * h * 4U, fill);
  return bmp;
}

std::vector<std::uint8_t> source_bytes(std::string_view tag) {
  return std::vector<std::uint8_t>(tag.begin(), tag.end());
}

} // namespace

int main() {
  using namespace instantee::asset_pipeline;
  using namespace instantee::diagnostics;

  AssetCompileFlags flags{};

  // Content hash changes with bytes and with flags.
  {
    const auto a = asset_content_sha256(source_bytes("png-a"), flags);
    const auto b = asset_content_sha256(source_bytes("png-b"), flags);
    expect(a.has_value() && b.has_value(), "hash ok");
    if (a && b) {
      expect(a->size() == 64, "hash length");
      expect(*a != *b, "different bytes different hash");
    }
    AssetCompileFlags other = flags;
    other.alpha_mode = AlphaMode::premultiplied;
    const auto c = asset_content_sha256(source_bytes("png-a"), other);
    expect(c.has_value() && a.has_value() && *c != *a, "flags change hash");
  }

  // Selective GPU invalidation: changing asset A leaves asset B bound.
  {
    AssetCache cache;
    const auto ha =
        cache.put_bitmap("A", source_bytes("A1"), flags, make_bitmap(2, 2, 10));
    const auto hb =
        cache.put_bitmap("B", source_bytes("B1"), flags, make_bitmap(2, 2, 20));
    expect(ha.has_value() && hb.has_value(), "put A/B");
    GpuAssetSlot *ga = cache.bind_gpu_for_asset("A");
    GpuAssetSlot *gb = cache.bind_gpu_for_asset("B");
    expect(ga != nullptr && gb != nullptr, "bind A/B");
    const std::uint64_t gen_b = gb->generation;
    const std::string hash_b = hb ? *hb : std::string{};

    const auto ha2 =
        cache.put_bitmap("A", source_bytes("A2"), flags, make_bitmap(2, 2, 11));
    expect(ha2.has_value() && ha.has_value() && *ha2 != *ha,
           "A content changed");
    expect(cache.stats().selective_invalidations == 1,
           "one selective invalidation");
    expect(!cache.gpu().contains(*ha), "old A gpu gone");
    expect(cache.gpu().contains(hash_b), "B gpu retained");
    const GpuAssetSlot *gb_after = cache.gpu().find(hash_b);
    expect(gb_after != nullptr && gb_after->generation == gen_b,
           "B generation unchanged");
    expect(cache.find_disk_for_asset("B") != nullptr, "B disk retained");
    expect(cache.find_disk_for_asset("A") != nullptr, "A new disk present");
    expect(cache.disk().contains(*ha2), "new A hash on disk");
  }

  // Explicit invalidate_asset drops GPU mapping for that id only.
  {
    AssetCache cache;
    expect(cache.put_bitmap("X", source_bytes("X"), flags, make_bitmap(1, 1, 1))
               .has_value(),
           "put X");
    expect(cache.put_bitmap("Y", source_bytes("Y"), flags, make_bitmap(1, 1, 2))
               .has_value(),
           "put Y");
    expect(cache.bind_gpu_for_asset("X") != nullptr, "bind X");
    expect(cache.bind_gpu_for_asset("Y") != nullptr, "bind Y");
    const auto hy = cache.content_hash_for("Y");
    cache.invalidate_asset("X");
    expect(!cache.content_hash_for("X").has_value(), "X id cleared");
    expect(cache.bind_gpu_for_asset("X") == nullptr, "X gpu unbound");
    expect(hy.has_value() && cache.gpu().contains(*hy), "Y gpu still bound");
  }

  // Durable blob round-trip; corruption and version skew fail closed.
  {
    NormalizedBitmapRecord record;
    auto hash = asset_content_sha256(source_bytes("blob"), flags);
    expect(hash.has_value(), "blob hash");
    if (!hash) {
      return EXIT_FAILURE;
    }
    record.content_hash = *hash;
    record.flags = flags;
    record.bitmap = make_bitmap(2, 1, 7);

    auto blob = AssetDiskCache::write_blob(record);
    expect(blob.has_value(), "write blob");
    if (blob) {
      auto round = AssetDiskCache::read_blob(*blob);
      expect(round.has_value(), "read blob");
      if (round) {
        expect(round->content_hash == record.content_hash, "blob hash match");
        expect(round->bitmap.width_px == 2 && round->bitmap.height_px == 1,
               "blob size");
        expect(round->bitmap.rgba.size() == 8, "blob rgba");
        expect(round->bitmap.rgba[0] == 7, "blob pixel");
      }

      auto bad_magic = *blob;
      bad_magic[0] = 'Z';
      auto magic_err = AssetDiskCache::read_blob(bad_magic);
      expect(!magic_err.has_value(), "bad magic rejected");
      if (!magic_err) {
        expect(magic_err.error().code == "asset_cache_bad_magic",
               "bad magic code");
        expect(magic_err.error().kind == ErrorKind::missing_or_corrupt_asset,
               "bad magic kind");
      }

      auto truncated =
          std::vector<std::uint8_t>(blob->begin(), blob->begin() + 6);
      auto trunc_err = AssetDiskCache::read_blob(truncated);
      expect(!trunc_err.has_value(), "truncated rejected");
      if (!trunc_err) {
        expect(trunc_err.error().code == "asset_cache_truncated",
               "truncate code");
      }

      auto bad_version = *blob;
      bad_version[4] = 9; // LE u32 version after magic
      auto ver_err = AssetDiskCache::read_blob(bad_version);
      expect(!ver_err.has_value(), "version rejected");
      if (!ver_err) {
        expect(ver_err.error().code == "asset_cache_version_mismatch",
               "version code");
        expect(ver_err.error().kind == ErrorKind::incompatible_schema,
               "version kind");
      }

      // Rebuild path: after a rejected blob, a fresh store succeeds.
      AssetDiskCache disk;
      expect(disk.store(record).has_value(), "store after reject");
      expect(disk.contains(*hash), "rebuilt entry present");
    }
  }

  // Same content under two ids shares disk/GPU hash; invalidating one id does
  // not drop the shared GPU slot while the other id still references it.
  {
    AssetCache cache;
    const auto h1 = cache.put_bitmap("p1", source_bytes("same"), flags,
                                     make_bitmap(1, 1, 3));
    const auto h2 = cache.put_bitmap("p2", source_bytes("same"), flags,
                                     make_bitmap(1, 1, 3));
    expect(h1.has_value() && h2.has_value() && *h1 == *h2,
           "shared content hash");
    expect(cache.bind_gpu_for_asset("p1") != nullptr, "bind p1");
    expect(cache.bind_gpu_for_asset("p2") != nullptr, "bind p2");
    cache.invalidate_asset("p1");
    expect(cache.find_disk_for_asset("p2") != nullptr, "p2 disk remains");
    expect(h2.has_value() && cache.disk().contains(*h2),
           "shared disk entry remains");
    expect(h2.has_value() && cache.gpu().contains(*h2),
           "shared gpu slot remains for p2");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
