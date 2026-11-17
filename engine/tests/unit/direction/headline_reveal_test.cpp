#include <instantee/direction/headline_reveal.hpp>
#include <instantee/direction/style_bible.hpp>
#include <instantee/typography/kinetic_layout.hpp>
#include <instantee/typography/text_shape.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

[[nodiscard]] std::string format_frame(
    std::uint32_t frame,
    const std::vector<instantee::direction::HeadlineUnitFrame> &units) {
  std::ostringstream ss;
  ss << "f=" << frame;
  for (const auto &u : units) {
    ss << " |u" << u.unit_index << " p=" << u.progress << " o=" << u.opacity
       << " m=" << u.mask << " s=" << u.scale << " oy=" << u.offset_y_26_6;
  }
  ss << '\n';
  return ss.str();
}

[[nodiscard]] std::string
golden_dump(const instantee::direction::HeadlineRevealProgram &program,
            std::string_view label) {
  using instantee::direction::sample_headline_reveal;
  std::ostringstream ss;
  ss << "preset=" << label << " mode="
     << instantee::direction::headline_reveal_mode_id(program.params.mode)
     << " units=" << program.unit_count
     << " duration=" << program.params.duration_frames
     << " intensity=" << program.params.intensity
     << " stagger=" << program.params.stagger_frames << '\n';
  const std::uint32_t samples[] = {0u, program.params.duration_frames / 4u,
                                   program.params.duration_frames / 2u,
                                   program.params.duration_frames};
  for (std::uint32_t frame : samples) {
    auto sampled = sample_headline_reveal(program, frame);
    if (!sampled) {
      ss << "ERROR\n";
      continue;
    }
    ss << format_frame(frame, *sampled);
  }
  return ss.str();
}

} // namespace

int main(int argc, char **argv) {
  using namespace instantee::direction;
  using namespace instantee::typography;

  const bool write_goldens =
      argc == 3 && std::string_view(argv[2]) == "--write-goldens";
  if (argc != 2 && !write_goldens) {
    std::cerr << "usage: headline_reveal_test <repo-root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};

  auto bible = load_style_bible_file(
      root / "contracts/fixtures/style-bible/positive/editorial.style.json");
  expect(bible.has_value(), "load editorial style");
  if (!bible) {
    return EXIT_FAILURE;
  }

  const auto hr_it = bible->motion().find("headline_reveal");
  const auto slam_it = bible->motion().find("accent_slam");
  expect(hr_it != bible->motion().end(), "headline_reveal token");
  expect(slam_it != bible->motion().end(), "accent_slam token");
  if (hr_it == bible->motion().end() || slam_it == bible->motion().end()) {
    return EXIT_FAILURE;
  }

  auto stagger_params =
      make_headline_reveal_params("headline_reveal", hr_it->second);
  expect(stagger_params.has_value(), "params headline_reveal");
  auto mask_params = make_headline_reveal_params("mask_reveal", hr_it->second);
  expect(mask_params.has_value(), "params mask_reveal");
  auto slam_params =
      make_headline_reveal_params("accent_slam", slam_it->second);
  expect(slam_params.has_value(), "params accent_slam");
  if (!stagger_params || !mask_params || !slam_params) {
    return EXIT_FAILURE;
  }
  expect(stagger_params->mode == HeadlineRevealMode::word_stagger,
         "stagger mode");
  expect(mask_params->mode == HeadlineRevealMode::mask_reveal, "mask mode");
  expect(slam_params->mode == HeadlineRevealMode::slam, "slam mode");
  expect(stagger_params->duration_frames == 18, "token duration");
  expect(std::abs(stagger_params->intensity - 0.8) < 1e-9, "token intensity");

  // Intensity/duration overrides stay in C++ presets (no frame-loop JS).
  auto scaled = scale_headline_reveal_params(*stagger_params, 0.5, 30);
  expect(scaled.has_value() && scaled->intensity == 0.5 &&
             scaled->duration_frames == 30,
         "scale params");
  expect(!scale_headline_reveal_params(*stagger_params, 0.0, 18),
         "reject zero intensity");
  expect(!scale_headline_reveal_params(*stagger_params, 0.8, 0),
         "reject zero duration");
  expect(!make_headline_reveal_params("unknown", hr_it->second),
         "reject unknown preset");

  // Word count from kinetic layout (composition point; direction stays free of
  // shaping backends).
  {
    const auto font_dir = root / "third_party/fonts";
    const std::filesystem::path stack[] = {font_dir / "NotoSans-Regular.ttf",
                                           font_dir / "DejaVuSans.ttf"};
    auto fonts = FontStack::open(stack);
    expect(fonts.has_value(), "open fonts");
    if (fonts) {
      constexpr std::string_view kHeadline = "Claim holds under stress";
      TextShaper shaper;
      auto shaped = shaper.shape(*fonts, kHeadline, 48.0f);
      expect(shaped.has_value(), "shape headline");
      if (shaped) {
        auto layout = build_kinetic_layout(kHeadline, *shaped);
        expect(layout.has_value() && layout->words.size() >= 3,
               "kinetic words");
        if (layout) {
          const std::uint32_t words =
              static_cast<std::uint32_t>(layout->words.size());
          auto program = compile_headline_reveal(*stagger_params, words);
          expect(program.has_value(), "compile stagger");
          if (program) {
            auto mid = sample_headline_reveal(
                *program, program->params.duration_frames / 2);
            expect(mid.has_value(), "sample mid");
            if (mid && mid->size() >= 2) {
              // Earlier words lead later ones under stagger.
              expect((*mid)[0].opacity >= (*mid)[1].opacity, "stagger lead");
              expect((*mid)[0].offset_y_26_6 <= (*mid)[1].offset_y_26_6 ||
                         (*mid)[1].progress < 1.0,
                     "later word still entering or equal");
            }
            auto again = sample_headline_reveal(
                *program, program->params.duration_frames / 2);
            expect(
                again.has_value() && mid.has_value() &&
                    format_frame(program->params.duration_frames / 2, *mid) ==
                        format_frame(program->params.duration_frames / 2,
                                     *again),
                "deterministic sample");

            const std::string golden = golden_dump(*program, "headline_reveal");
            const auto golden_path =
                root /
                "contracts/fixtures/headline-reveal/goldens/word-stagger.txt";
            if (write_goldens) {
              std::filesystem::create_directories(golden_path.parent_path());
              std::ofstream out(golden_path,
                                std::ios::binary | std::ios::trunc);
              out << golden;
              expect(static_cast<bool>(out), "write stagger golden");
            } else {
              expect(write_if_missing(golden_path, golden), "seed stagger");
              expect(read_file(golden_path) == golden, "stagger golden match");
            }
          }
        }
      }
    }
  }

  // Mask reveal sequence.
  {
    auto program = compile_headline_reveal(*mask_params, 3);
    expect(program.has_value(), "compile mask");
    if (program) {
      auto early = sample_headline_reveal(*program, 0);
      auto late =
          sample_headline_reveal(*program, program->params.duration_frames);
      expect(early && late, "mask samples");
      if (early && late) {
        expect((*early)[0].mask == 0.0 && (*late)[0].mask == 1.0, "mask range");
        expect((*late)[2].opacity == mask_params->intensity, "mask peak");
      }
      const std::string golden = golden_dump(*program, "mask_reveal");
      const auto golden_path =
          root / "contracts/fixtures/headline-reveal/goldens/mask-reveal.txt";
      if (write_goldens) {
        std::filesystem::create_directories(golden_path.parent_path());
        std::ofstream out(golden_path, std::ios::binary | std::ios::trunc);
        out << golden;
        expect(static_cast<bool>(out), "write mask golden");
      } else {
        expect(write_if_missing(golden_path, golden), "seed mask");
        expect(read_file(golden_path) == golden, "mask golden match");
      }

      std::vector<std::string> targets{"w0", "w1", "w2"};
      auto tracks = lower_headline_reveal_tracks(*program, "shot0", targets);
      expect(tracks.has_value() && tracks->size() == 6, "mask tracks");
    }
  }

  // Slam typography.
  {
    auto program = compile_headline_reveal(*slam_params, 1);
    expect(program.has_value(), "compile slam");
    expect(!compile_headline_reveal(*slam_params, 2), "slam rejects multi");
    if (program) {
      auto start = sample_headline_reveal(*program, 0);
      auto end =
          sample_headline_reveal(*program, program->params.duration_frames);
      expect(start && end, "slam samples");
      if (start && end) {
        expect((*start)[0].scale > 1.0, "slam overshoot");
        expect(std::abs((*end)[0].scale - 1.0) < 1e-9, "slam settle");
        expect((*end)[0].opacity == slam_params->intensity, "slam opacity");
      }
      auto hot = scale_headline_reveal_params(*slam_params, 1.0, 6);
      expect(hot.has_value(), "hot slam");
      if (hot) {
        auto hot_prog = compile_headline_reveal(*hot, 1);
        expect(hot_prog.has_value(), "compile hot slam");
        if (hot_prog) {
          expect(hot_prog->params.duration_frames == 6, "duration override");
          expect(std::abs(hot_prog->params.intensity - 1.0) < 1e-9,
                 "intensity override");
        }
      }
      const std::string golden = golden_dump(*program, "accent_slam");
      const auto golden_path =
          root / "contracts/fixtures/headline-reveal/goldens/accent-slam.txt";
      if (write_goldens) {
        std::filesystem::create_directories(golden_path.parent_path());
        std::ofstream out(golden_path, std::ios::binary | std::ios::trunc);
        out << golden;
        expect(static_cast<bool>(out), "write slam golden");
      } else {
        expect(write_if_missing(golden_path, golden), "seed slam");
        expect(read_file(golden_path) == golden, "slam golden match");
      }
    }
  }

  expect(!compile_headline_reveal(*stagger_params, 0), "reject empty units");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
