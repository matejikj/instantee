#include <instantee/render_core/photo_crop.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using namespace instantee::render_core;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] Rgba8 at(const Rgba8Image &img, std::uint32_t x,
                       std::uint32_t y) {
  const std::size_t idx = (static_cast<std::size_t>(y) * img.width_px + x) * 4U;
  return Rgba8{img.rgba[idx], img.rgba[idx + 1], img.rgba[idx + 2],
               img.rgba[idx + 3]};
}

[[nodiscard]] bool same_rgb(const Rgba8 &c, std::uint8_t r, std::uint8_t g,
                            std::uint8_t b) {
  return c.r == r && c.g == g && c.b == b;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

[[nodiscard]] bool write_if_missing(const std::filesystem::path &path,
                                    std::string_view body) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] std::string golden_dump(std::string_view name,
                                      const PhotoCropPlan &plan,
                                      const Rgba8Image &img) {
  std::ostringstream ss;
  ss << "name=" << name << " fit=" << photo_fit_mode_id(plan.fit)
     << " cutout=" << photo_cutout_kind_id(plan.cutout)
     << " scale=" << plan.scale << '\n';
  ss << "image_dest x=" << plan.image_dest.x << " y=" << plan.image_dest.y
     << " w=" << plan.image_dest.width << " h=" << plan.image_dest.height
     << '\n';
  ss << "visible x=" << plan.visible_intersection.x
     << " y=" << plan.visible_intersection.y
     << " w=" << plan.visible_intersection.width
     << " h=" << plan.visible_intersection.height << '\n';
  ss << "overflow=" << (plan.clipping_overflows_frame ? 1 : 0) << '\n';
  // Sample a few deterministic pixels for golden comparison.
  const std::uint32_t samples[][2] = {
      {0, 0}, {16, 16}, {32, 24}, {48, 40}, {63, 47}};
  for (const auto &s : samples) {
    const Rgba8 c = at(img, s[0], s[1]);
    ss << "p" << s[0] << "," << s[1] << "=" << static_cast<int>(c.r) << ","
       << static_cast<int>(c.g) << "," << static_cast<int>(c.b) << ","
       << static_cast<int>(c.a) << '\n';
  }
  return ss.str();
}

[[nodiscard]] bool
pixels_outside_frame_are_background(const Rgba8Image &img, const Rect2f &frame,
                                    const Rgba8 &background) {
  const auto inside = [&](std::uint32_t x, std::uint32_t y) {
    return static_cast<float>(x) >= frame.x &&
           static_cast<float>(y) >= frame.y &&
           static_cast<float>(x) < frame.x + frame.width &&
           static_cast<float>(y) < frame.y + frame.height;
  };
  for (std::uint32_t y = 0; y < img.height_px; ++y) {
    for (std::uint32_t x = 0; x < img.width_px; ++x) {
      if (inside(x, y)) {
        continue;
      }
      const Rgba8 c = at(img, x, y);
      // Edge framing may paint on the frame border; outside the frame AABB
      // must remain background (no clip overflow).
      if (!same_rgb(c, background.r, background.g, background.b)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr << "usage: photo_crop_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir = root / "contracts/fixtures/photo-crop/goldens";

  // Cover: wide source into a tall frame — image extends past left/right, but
  // clipping must keep painted pixels inside the frame.
  {
    PhotoCropRequest req;
    req.source_width_px = 200;
    req.source_height_px = 100;
    req.frame = {16, 8, 32, 32};
    req.fit = PhotoFitMode::cover;
    req.photo_color = {200, 40, 40, 255};
    req.background = {10, 10, 10, 255};
    req.canvas_width_px = 64;
    req.canvas_height_px = 48;
    auto plan = plan_photo_crop(req);
    expect(plan.has_value(), "plan cover");
    if (plan) {
      expect(!plan->clipping_overflows_frame, "cover no overflow flag");
      expect(plan->image_dest.width > req.frame.width,
             "cover wider than frame");
      auto script = build_photo_crop_clip_script(req, *plan);
      expect(script.has_value(), "script cover");
      if (script) {
        auto img = rasterize_clip_script(*script);
        expect(img.has_value(), "raster cover");
        if (img) {
          expect(pixels_outside_frame_are_background(*img, req.frame,
                                                     req.background),
                 "cover no paint outside frame");
          expect(same_rgb(at(*img, 32, 24), 200, 40, 40), "cover center photo");
          expect(same_rgb(at(*img, 0, 0), 10, 10, 10), "cover corner bg");
          const std::string golden = golden_dump("cover", *plan, *img);
          const auto path = golden_dir / "cover.txt";
          if (write_goldens) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << golden;
            expect(static_cast<bool>(out), "write cover golden");
          } else {
            expect(write_if_missing(path, golden), "seed cover");
            expect(read_file(path) == golden, "cover golden match");
          }
        }
      }
    }
  }

  // Contain: image stays inside frame without relying on overflow.
  {
    PhotoCropRequest req;
    req.source_width_px = 200;
    req.source_height_px = 100;
    req.frame = {8, 8, 48, 32};
    req.fit = PhotoFitMode::contain;
    req.photo_color = {40, 160, 40, 255};
    req.background = {20, 20, 20, 255};
    req.canvas_width_px = 64;
    req.canvas_height_px = 48;
    auto plan = plan_photo_crop(req);
    expect(plan.has_value(), "plan contain");
    if (plan) {
      expect(plan->image_dest.width <= req.frame.width + 1e-3f &&
                 plan->image_dest.height <= req.frame.height + 1e-3f,
             "contain inside");
      auto script = build_photo_crop_clip_script(req, *plan);
      expect(script.has_value(), "script contain");
      if (script) {
        auto img = rasterize_clip_script(*script);
        expect(img.has_value(), "raster contain");
        if (img) {
          expect(pixels_outside_frame_are_background(*img, req.frame,
                                                     req.background),
                 "contain no overflow");
          const std::string golden = golden_dump("contain", *plan, *img);
          const auto path = golden_dir / "contain.txt";
          if (write_goldens) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << golden;
            expect(static_cast<bool>(out), "write contain golden");
          } else {
            expect(write_if_missing(path, golden), "seed contain");
            expect(read_file(path) == golden, "contain golden match");
          }
        }
      }
    }
  }

  // Ellipse cutout + sharp edge framing.
  {
    PhotoCropRequest req;
    req.source_width_px = 64;
    req.source_height_px = 64;
    req.frame = {12, 8, 40, 32};
    req.fit = PhotoFitMode::cover;
    req.cutout = PhotoCutoutKind::ellipse;
    req.edge_frame_px = 2;
    req.photo_color = {40, 40, 200, 255};
    req.edge_color = {0, 0, 0, 255};
    req.background = {240, 240, 240, 255};
    req.canvas_width_px = 64;
    req.canvas_height_px = 48;
    auto plan = plan_photo_crop(req);
    expect(plan.has_value(), "plan cutout");
    if (plan) {
      auto script = build_photo_crop_clip_script(req, *plan);
      expect(script.has_value(), "script cutout");
      if (script) {
        auto stats = clip_script_stats(*script);
        expect(stats.has_value() && stats->mask_pushes == 1 &&
                   stats->scissor_pushes == 1,
               "cutout stack");
        auto img = rasterize_clip_script(*script);
        expect(img.has_value(), "raster cutout");
        if (img) {
          // Outside the frame AABB remains background (edge sits on frame).
          expect(same_rgb(at(*img, 0, 0), 240, 240, 240), "cutout outer bg");
          // Frame border pixel should be edge color.
          expect(same_rgb(at(*img, 12, 8), 0, 0, 0), "edge top-left");
          const std::string golden = golden_dump("ellipse-edge", *plan, *img);
          const auto path = golden_dir / "ellipse-edge.txt";
          if (write_goldens) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << golden;
            expect(static_cast<bool>(out), "write ellipse golden");
          } else {
            expect(write_if_missing(path, golden), "seed ellipse");
            expect(read_file(path) == golden, "ellipse golden match");
          }
        }
      }
    }
  }

  // Rounded rect cutout.
  {
    PhotoCropRequest req;
    req.source_width_px = 80;
    req.source_height_px = 40;
    req.frame = {8, 8, 48, 32};
    req.fit = PhotoFitMode::cover;
    req.cutout = PhotoCutoutKind::rounded_rect;
    req.cutout_corner_radius = 6;
    req.photo_color = {180, 120, 20, 255};
    req.background = {0, 0, 0, 255};
    req.canvas_width_px = 64;
    req.canvas_height_px = 48;
    auto plan = plan_photo_crop(req);
    expect(plan.has_value(), "plan rounded");
    if (plan) {
      auto script = build_photo_crop_clip_script(req, *plan);
      expect(script.has_value(), "script rounded");
      if (script) {
        auto img = rasterize_clip_script(*script);
        expect(img.has_value(), "raster rounded");
        if (img) {
          expect(pixels_outside_frame_are_background(*img, req.frame,
                                                     req.background),
                 "rounded no overflow");
          const std::string golden = golden_dump("rounded", *plan, *img);
          const auto path = golden_dir / "rounded.txt";
          if (write_goldens) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << golden;
            expect(static_cast<bool>(out), "write rounded golden");
          } else {
            expect(write_if_missing(path, golden), "seed rounded");
            expect(read_file(path) == golden, "rounded golden match");
          }
        }
      }
    }
  }

  // Error paths.
  expect(!plan_photo_crop(PhotoCropRequest{}), "reject empty");
  {
    PhotoCropRequest bad;
    bad.source_width_px = 10;
    bad.source_height_px = 10;
    bad.frame = {0, 0, 8, 8};
    bad.edge_frame_px = 5;
    bad.canvas_width_px = 16;
    bad.canvas_height_px = 16;
    expect(!plan_photo_crop(bad), "reject thick edge");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
