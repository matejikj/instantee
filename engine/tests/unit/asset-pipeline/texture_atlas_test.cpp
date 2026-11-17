#include <instantee/asset_pipeline/texture_atlas.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::vector<std::uint8_t> solid(std::uint32_t w, std::uint32_t h,
                                std::array<std::uint8_t, 4> rgba) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4U);
  for (std::size_t i = 0; i < out.size(); i += 4) {
    out[i] = rgba[0];
    out[i + 1] = rgba[1];
    out[i + 2] = rgba[2];
    out[i + 3] = rgba[3];
  }
  return out;
}

} // namespace

int main() {
  using namespace instantee::asset_pipeline;

  TextureAtlasConfig cfg;
  cfg.page_width_px = 64;
  cfg.page_height_px = 64;
  cfg.padding_px = 1;
  cfg.max_pages = 2;
  cfg.mip_policy = AtlasMipPolicy::none;
  cfg.sample_filter = AtlasSampleFilter::linear;

  TextureAtlas atlas(cfg);
  const auto red = solid(8, 8, {255, 0, 0, 255});
  const auto blue = solid(8, 8, {0, 0, 255, 255});

  expect(atlas.insert("red", 8, 8, red).has_value(), "insert red");
  expect(atlas.insert("blue", 8, 8, blue).has_value(), "insert blue");
  const AtlasSprite *a = atlas.find("red");
  const AtlasSprite *b = atlas.find("blue");
  expect(a != nullptr && b != nullptr, "find red/blue");
  if (a == nullptr || b == nullptr) {
    return EXIT_FAILURE;
  }

  // Hit rate: second insert of same key is a hit.
  expect(atlas.insert("red", 8, 8, red).has_value(), "reinsert red");
  expect(atlas.stats().insert_hits == 1, "one hit");
  expect(atlas.stats().insert_misses == 2, "two misses");

  // No pixel bleeding: exclusive sprite interiors stay pure; gutter is clear.
  {
    const AtlasRect ra = a->rect;
    const AtlasRect rb = b->rect;
    auto sample = [&](std::uint32_t x, std::uint32_t y) {
      return atlas.sample_page(ra.page, x, y);
    };
    auto px = sample(ra.x_px, ra.y_px);
    expect(px.has_value() && (*px)[0] == 255 && (*px)[2] == 0, "red corner");
    px = sample(ra.x_px + ra.width_px - 1, ra.y_px + ra.height_px - 1);
    expect(px.has_value() && (*px)[0] == 255, "red far corner");

    // Right gutter of red (between red and next sprite) must be zeroed.
    px = sample(ra.x_px + ra.width_px, ra.y_px);
    expect(px.has_value() && (*px)[0] == 0 && (*px)[1] == 0 && (*px)[2] == 0 &&
               (*px)[3] == 0,
           "red right gutter clear");
    // Left gutter of blue.
    px = atlas.sample_page(rb.page, rb.x_px - 1, rb.y_px);
    expect(px.has_value() && (*px)[0] == 0 && (*px)[3] == 0,
           "blue left gutter clear");
    px = atlas.sample_page(rb.page, rb.x_px, rb.y_px);
    expect(px.has_value() && (*px)[2] == 255, "blue interior");
  }

  // Upload batching: first plan stages both sprites; after mark_resident a
  // second plan is empty (no per-frame reinstanciation).
  {
    auto batch = atlas.plan_uploads();
    expect(batch.has_value(), "plan uploads");
    if (batch) {
      expect(batch->regions.size() == 2, "two upload regions");
      expect(batch->staging_bytes == 8U * 8U * 4U * 2U, "staging bytes");
      expect(atlas.stats().staging_bytes_planned == batch->staging_bytes,
             "planned counter");
      atlas.mark_uploads_resident(*batch);
      const AtlasSprite *ar = atlas.find("red");
      const AtlasSprite *br = atlas.find("blue");
      expect(ar != nullptr && br != nullptr && ar->resident && br->resident,
             "sprites resident");
    }
    auto again = atlas.plan_uploads();
    expect(again.has_value() && again->regions.empty() &&
               again->staging_bytes == 0,
           "warm plan empty");
    expect(atlas.insert("red", 8, 8, red).has_value() &&
               atlas.stats().resident_reuses >= 1,
           "resident reuse counted");
  }

  // Padding 0 rejected.
  {
    TextureAtlasConfig bad = cfg;
    bad.padding_px = 0;
    TextureAtlas broken(bad);
    const auto err = broken.insert("x", 2, 2, solid(2, 2, {1, 2, 3, 4}));
    expect(!err.has_value() && err.error().code == "atlas_bad_config",
           "padding 0 rejected");
  }

  // Mip chain generated when requested.
  {
    TextureAtlasConfig mip_cfg = cfg;
    mip_cfg.mip_policy = AtlasMipPolicy::generate_box;
    mip_cfg.page_width_px = 32;
    mip_cfg.page_height_px = 32;
    TextureAtlas mip_atlas(mip_cfg);
    expect(mip_atlas.insert("m", 8, 8, red).has_value(), "mip insert");
    expect(mip_atlas.page_count() == 1, "one page");
    const AtlasPage *pg = mip_atlas.page(0);
    expect(pg != nullptr && !pg->mips.empty(), "mips present");
    if (pg && !pg->mips.empty()) {
      expect(pg->mips.front().width_px == 16, "first mip width");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
