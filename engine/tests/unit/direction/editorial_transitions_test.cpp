#include <instantee/direction/editorial_transitions.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace instantee::direction;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
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

[[nodiscard]] EditorialTransitionRequest
base_request(EditorialTransitionPreset preset, std::uint64_t duration) {
  EditorialTransitionRequest req;
  req.id = "t1";
  req.from_shot_id = "a";
  req.to_shot_id = "b";
  req.preset = preset;
  req.duration_frames = duration;
  req.intensity = 0.6;
  req.canvas_width_px = 1080;
  req.canvas_height_px = 1920;
  req.safe_area = SafeAreaPolicy{.mode = SafeAreaMode::reels_overlay,
                                 .top_px = 120,
                                 .bottom_px = 160,
                                 .left_px = 48,
                                 .right_px = 48};
  req.type_focus = ContentSafeRect{
      .x_px = 120, .y_px = 400, .width_px = 840, .height_px = 160};
  return req;
}

[[nodiscard]] std::string golden_dump(std::string_view name,
                                      const EditorialTransitionVerification &v,
                                      const EditorialTransitionProgram &p) {
  std::ostringstream ss;
  ss << "name=" << name
     << " preset=" << editorial_transition_preset_id(p.preset)
     << " emphasis=" << typographic_emphasis_id(p.emphasis)
     << " packed=" << v.packed_duration_frames
     << " planned=" << v.plan.film_duration_frames
     << " preserved=" << (v.duration_preserved ? 1 : 0)
     << " safe=" << (v.safe_areas_ok ? 1 : 0) << " frames=" << v.frames.size()
     << '\n';
  if (!v.plan.spans.empty()) {
    const auto &s = v.plan.spans[0];
    ss << "span start=" << s.start_frame << " dur=" << s.duration_frames
       << " blend=" << s.blend << " kind=" << transition_kind_id(s.kind)
       << '\n';
  }
  const std::size_t indices[] = {0, v.frames.size() / 2,
                                 v.frames.empty() ? 0 : v.frames.size() - 1};
  for (std::size_t idx : indices) {
    if (idx >= v.frames.size()) {
      continue;
    }
    const auto &f = v.frames[idx];
    ss << "f" << f.local_frame << " p=" << f.progress
       << " wipe=" << f.wipe_coverage << " zoom=" << f.zoom_scale
       << " type_s=" << f.type_scale << " type_o=" << f.type_opacity
       << " inside=" << (f.type_inside_safe_area ? 1 : 0) << '\n';
  }
  return ss.str();
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr
        << "usage: editorial_transitions_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto golden_dir =
      root / "contracts/fixtures/editorial-transitions/goldens";

  std::vector<ShotTiming> shots{
      {.id = "a", .start_frame = 0, .duration_frames = 90},
      {.id = "b", .start_frame = 90, .duration_frames = 60},
  };
  const std::uint64_t packed = 150;

  // Hard cut: exact duration, zero-length span.
  {
    auto req = base_request(EditorialTransitionPreset::hard_cut, 0);
    auto program = compile_editorial_transition(req);
    expect(program.has_value(), "compile hard cut");
    if (program) {
      auto ver = verify_editorial_transition(shots, *program);
      expect(ver.has_value(), "verify hard cut");
      if (ver) {
        expect(ver->duration_preserved &&
                   ver->plan.film_duration_frames == packed,
               "hard cut frame count");
        expect(ver->safe_areas_ok, "hard cut safe");
        expect(ver->plan.spans[0].duration_frames == 0, "hard cut span 0");
      }
    }
  }

  // Crop wipe + mask_push emphasis golden.
  {
    auto req = base_request(EditorialTransitionPreset::crop_wipe, 20);
    req.emphasis = TypographicEmphasis::mask_push;
    auto program = compile_editorial_transition(req);
    expect(program.has_value(), "compile wipe");
    if (program) {
      auto ver = verify_editorial_transition(shots, *program);
      expect(ver.has_value(), "verify wipe");
      if (ver) {
        expect(ver->duration_preserved &&
                   ver->plan.film_duration_frames == packed,
               "wipe frame count");
        expect(ver->safe_areas_ok && ver->frames.size() == 20, "wipe frames");
        expect(ver->plan.spans[0].blend == "wipe", "wipe blend");
        const std::string golden = golden_dump("crop-wipe", *ver, *program);
        const auto path = golden_dir / "crop-wipe.txt";
        if (write_goldens) {
          std::filesystem::create_directories(path.parent_path());
          std::ofstream out(path, std::ios::binary | std::ios::trunc);
          out << golden;
          expect(static_cast<bool>(out), "write wipe golden");
        } else {
          expect(write_if_missing(path, golden), "seed wipe");
          expect(read_file(path) == golden, "wipe golden");
        }
      }
    }
  }

  // Rapid zoom + slam_in: duration preserved, safe areas hold.
  {
    auto req = base_request(EditorialTransitionPreset::rapid_zoom, 16);
    req.emphasis = TypographicEmphasis::slam_in;
    auto program = compile_editorial_transition(req);
    expect(program.has_value(), "compile zoom");
    if (program) {
      auto ver = verify_editorial_transition(shots, *program);
      expect(ver.has_value(), "verify zoom");
      if (ver) {
        expect(ver->duration_preserved &&
                   ver->plan.film_duration_frames == packed,
               "zoom frame count");
        expect(ver->safe_areas_ok, "zoom safe");
        expect(ver->plan.spans[0].blend == "dissolve", "zoom → dissolve");
        const std::string golden = golden_dump("rapid-zoom", *ver, *program);
        const auto path = golden_dir / "rapid-zoom.txt";
        if (write_goldens) {
          std::filesystem::create_directories(path.parent_path());
          std::ofstream out(path, std::ios::binary | std::ios::trunc);
          out << golden;
          expect(static_cast<bool>(out), "write zoom golden");
        } else {
          expect(write_if_missing(path, golden), "seed zoom");
          expect(read_file(path) == golden, "zoom golden");
        }
      }
    }
  }

  // Camera snap: match cut, duration intact, safe clamp.
  {
    auto req = base_request(EditorialTransitionPreset::camera_snap, 0);
    auto program = compile_editorial_transition(req);
    expect(program.has_value(), "compile snap");
    if (program) {
      auto ver = verify_editorial_transition(shots, *program);
      expect(ver.has_value(), "verify snap");
      if (ver) {
        expect(ver->duration_preserved &&
                   ver->plan.film_duration_frames == packed,
               "snap frame count");
        expect(ver->safe_areas_ok, "snap safe");
        expect(ver->plan.spans[0].kind == TransitionKind::match_cut,
               "snap kind");
      }
    }
  }

  // Reject type focus outside safe area at compile time.
  {
    auto req = base_request(EditorialTransitionPreset::hard_cut, 0);
    req.type_focus.y_px = 0; // overlaps top gutter
    expect(!compile_editorial_transition(req).has_value(),
           "reject unsafe focus");
  }

  // Reject wipe with zero duration.
  {
    auto req = base_request(EditorialTransitionPreset::crop_wipe, 0);
    expect(!compile_editorial_transition(req).has_value(), "reject zero wipe");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
