#include <instantee/asset_pipeline/bitmap_decode.hpp>
#include <instantee/asset_pipeline/svg_import.hpp>
#include <instantee/typography/text_shape.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bounded asset-import fuzz driver. Exercises SVG security+import, PNG/WebP
// decode with tight dimension budgets, and FreeType font open on temp files.
// Importers must fail closed (Error) — no network fetch, no script execution,
// no uncontrolled pixel/path allocation. Decode wall time is sampled; the
// ctest TIMEOUT is the hard hang guard.
namespace {

namespace fs = std::filesystem;
using clock = std::chrono::steady_clock;

struct Stats {
  std::uint64_t inputs = 0;
  std::uint64_t svg_ok = 0;
  std::uint64_t svg_err = 0;
  std::uint64_t svg_security_reject = 0;
  std::uint64_t bitmap_ok = 0;
  std::uint64_t bitmap_err = 0;
  std::uint64_t font_ok = 0;
  std::uint64_t font_err = 0;
  std::uint64_t slow_ops = 0;
  std::uint64_t max_decode_ms = 0;
};

constexpr auto kDecodeWarn = std::chrono::milliseconds(500);

void note_timing(Stats *stats, clock::time_point started) {
  if (stats == nullptr) {
    return;
  }
  const auto ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() -
                                                            started)
          .count());
  stats->max_decode_ms = std::max(stats->max_decode_ms, ms);
  if (clock::now() - started > kDecodeWarn) {
    ++stats->slow_ops;
  }
}

void run_svg(std::span<const std::uint8_t> bytes, Stats *stats) {
  instantee::asset_pipeline::SvgImportLimits limits;
  limits.max_input_bytes = 256U * 1024U;
  limits.max_paths = 512;
  limits.max_commands = 32U * 1024U;
  limits.max_extent = 4096.0f;

  const auto started = clock::now();
  const auto security =
      instantee::asset_pipeline::validate_svg_security(bytes, limits);
  if (!security) {
    if (stats != nullptr) {
      ++stats->svg_err;
      ++stats->svg_security_reject;
    }
    note_timing(stats, started);
    return;
  }
  const auto imported = instantee::asset_pipeline::import_svg(bytes, limits);
  if (stats != nullptr) {
    if (imported) {
      ++stats->svg_ok;
    } else {
      ++stats->svg_err;
    }
  }
  note_timing(stats, started);
}

void run_bitmap(std::span<const std::uint8_t> bytes, Stats *stats) {
  instantee::asset_pipeline::BitmapDecodeOptions options;
  options.limits.max_input_bytes = 256U * 1024U;
  options.limits.max_width_px = 1024;
  options.limits.max_height_px = 1024;
  options.limits.max_pixels = 512U * 512U;

  const auto started = clock::now();
  (void)instantee::asset_pipeline::probe_bitmap_dimensions(bytes,
                                                           options.limits);
  const auto decoded = instantee::asset_pipeline::decode_bitmap(bytes, options);
  if (stats != nullptr) {
    if (decoded) {
      ++stats->bitmap_ok;
      // Accepted decode must respect the declared ceilings.
      if (decoded->width_px > options.limits.max_width_px ||
          decoded->height_px > options.limits.max_height_px ||
          static_cast<std::uint64_t>(decoded->width_px) *
                  static_cast<std::uint64_t>(decoded->height_px) >
              options.limits.max_pixels) {
        std::abort(); // uncontrolled dimensions — fail closed loudly
      }
    } else {
      ++stats->bitmap_err;
    }
  }
  note_timing(stats, started);
}

void run_font(std::span<const std::uint8_t> bytes, Stats *stats,
              const fs::path &sandbox) {
  // Cap font blob size before touching FreeType.
  constexpr std::size_t kMaxFontBytes = 512U * 1024U;
  if (bytes.size() > kMaxFontBytes) {
    bytes = bytes.first(kMaxFontBytes);
  }

  const auto started = clock::now();
  const fs::path face_path = sandbox / "fuzz-face.bin";
  {
    std::ofstream out(face_path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }
  const fs::path paths[] = {face_path};
  const auto opened = instantee::typography::FontStack::open(
      std::span<const fs::path>(paths, 1));
  if (stats != nullptr) {
    if (opened) {
      ++stats->font_ok;
    } else {
      ++stats->font_err;
    }
  }
  note_timing(stats, started);
  std::error_code ec;
  fs::remove(face_path, ec);
}

enum class Kind : std::uint8_t { auto_detect, svg, bitmap, font };

[[nodiscard]] Kind sniff_kind(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' &&
      bytes[2] == 'N' && bytes[3] == 'G') {
    return Kind::bitmap;
  }
  if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' &&
      bytes[2] == 'F' && bytes[3] == 'F' && bytes[8] == 'W' &&
      bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') {
    return Kind::bitmap;
  }
  if (bytes.size() >= 4 && bytes[0] == 0x00 && bytes[1] == 0x01 &&
      bytes[2] == 0x00 && bytes[3] == 0x00) {
    return Kind::font; // sfnt
  }
  if (bytes.size() >= 4 && bytes[0] == 'O' && bytes[1] == 'T' &&
      bytes[2] == 'T' && bytes[3] == 'O') {
    return Kind::font;
  }
  // Cheap SVG sniff.
  const std::string_view text(reinterpret_cast<const char *>(bytes.data()),
                              std::min<std::size_t>(bytes.size(), 256));
  if (text.find("<svg") != std::string_view::npos ||
      text.find("<SVG") != std::string_view::npos) {
    return Kind::svg;
  }
  return Kind::auto_detect;
}

int run_once(const std::uint8_t *data, std::size_t size, Stats *stats,
             Kind force, const fs::path &sandbox) {
  if (stats != nullptr) {
    ++stats->inputs;
  }
  constexpr std::size_t kHardCap = 512U * 1024U;
  if (size > kHardCap) {
    size = kHardCap;
  }
  const std::span<const std::uint8_t> bytes(data, size);
  Kind kind = force;
  if (kind == Kind::auto_detect) {
    kind = sniff_kind(bytes);
  }

  switch (kind) {
  case Kind::svg:
    run_svg(bytes, stats);
    break;
  case Kind::bitmap:
    run_bitmap(bytes, stats);
    break;
  case Kind::font:
    run_font(bytes, stats, sandbox);
    break;
  case Kind::auto_detect:
    // Unknown: try all three fail-closed paths.
    run_svg(bytes, stats);
    run_bitmap(bytes, stats);
    run_font(bytes, stats, sandbox);
    break;
  }
  return 0;
}

[[nodiscard]] std::vector<std::uint8_t> read_file(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
}

void collect_seeds(const fs::path &root, std::vector<fs::path> &out) {
  if (!fs::exists(root)) {
    return;
  }
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    out.push_back(entry.path());
  }
  std::sort(out.begin(), out.end());
}

[[nodiscard]] Kind kind_from_path(const fs::path &path) noexcept {
  const auto parent = path.parent_path().filename().string();
  if (parent == "svg") {
    return Kind::svg;
  }
  if (parent == "bitmap") {
    return Kind::bitmap;
  }
  if (parent == "font") {
    return Kind::font;
  }
  return Kind::auto_detect;
}

[[nodiscard]] std::vector<std::uint8_t> mutate(std::vector<std::uint8_t> input,
                                               std::uint32_t which,
                                               std::size_t max_bytes) {
  if (input.size() > max_bytes) {
    input.resize(max_bytes);
  }
  switch (which % 10U) {
  case 0:
    return input;
  case 1:
    if (!input.empty()) {
      input[which % input.size()] ^= static_cast<std::uint8_t>(0x5A + which);
    }
    return input;
  case 2:
    if (input.size() > 2U) {
      input.resize(input.size() / 2U);
    }
    return input;
  case 3: {
    const char *svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>)";
    return std::vector<std::uint8_t>(svg, svg + std::strlen(svg));
  }
  case 4: {
    const char *svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg"><image href="https://example.invalid/x.png" width="10" height="10"/></svg>)";
    return std::vector<std::uint8_t>(svg, svg + std::strlen(svg));
  }
  case 5: {
    const char *svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg"><a href="file:///etc/passwd"><rect width="1" height="1"/></a></svg>)";
    return std::vector<std::uint8_t>(svg, svg + std::strlen(svg));
  }
  case 6: {
    // Truncated PNG IHDR claiming huge dimensions.
    return std::vector<std::uint8_t>{
        137, 80,  78,  71, 13, 10,  26,  10, 0, 0, 0, 13, 73, 72, 68,  82, 0,
        1,   134, 160, 0,  1,  134, 160, 8,  2, 0, 0, 0,  39, 48, 156, 159};
  }
  case 7:
    return std::vector<std::uint8_t>(max_bytes, static_cast<std::uint8_t>('A'));
  case 8:
    return {};
  default:
    input.insert(input.end(), {0x00, 0xFF, '<', '>'});
    if (input.size() > max_bytes) {
      input.resize(max_bytes);
    }
    return input;
  }
}

int run_corpus(const fs::path &corpus_root, std::uint32_t mutations,
               Stats &stats, const fs::path &sandbox) {
  std::vector<fs::path> seeds;
  collect_seeds(corpus_root, seeds);
  if (seeds.empty()) {
    std::cerr << "fuzz-asset: no corpus seeds\n";
    return 2;
  }
  constexpr std::size_t kMax = 512U * 1024U;
  for (const auto &seed : seeds) {
    auto base = read_file(seed);
    const Kind forced = kind_from_path(seed);
    const std::uint32_t rounds = std::max<std::uint32_t>(1U, mutations);
    for (std::uint32_t i = 0; i < rounds; ++i) {
      auto sample = mutate(base, i, kMax);
      (void)run_once(sample.data(), sample.size(), &stats, forced, sandbox);
    }
  }
  return 0;
}

void print_stats(const Stats &stats) {
  std::cout << "asset_fuzz_report"
            << " inputs=" << stats.inputs << " svg_ok=" << stats.svg_ok
            << " svg_err=" << stats.svg_err
            << " svg_security_reject=" << stats.svg_security_reject
            << " bitmap_ok=" << stats.bitmap_ok
            << " bitmap_err=" << stats.bitmap_err
            << " font_ok=" << stats.font_ok << " font_err=" << stats.font_err
            << " slow_ops=" << stats.slow_ops
            << " max_decode_ms=" << stats.max_decode_ms << '\n';
}

} // namespace

#if defined(INSTANTEE_FUZZ_STANDALONE)
int main(int argc, char **argv) {
  fs::path corpus;
  std::uint32_t mutations = 8;
  const char *single = nullptr;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--corpus" && i + 1 < argc) {
      corpus = argv[++i];
    } else if (arg == "--mutations" && i + 1 < argc) {
      mutations =
          static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
      if (mutations == 0U) {
        mutations = 1U;
      }
      if (mutations > 128U) {
        mutations = 128U;
      }
    } else if (!arg.empty() && arg[0] != '-') {
      single = argv[i];
    } else {
      std::cerr
          << "usage: fuzz-asset-import-smoke [--corpus DIR] [--mutations N] "
             "[file]\n";
      return 2;
    }
  }

  std::error_code ec;
  const fs::path sandbox =
      fs::temp_directory_path(ec) / "instantee-fuzz-asset-import";
  fs::create_directories(sandbox, ec);

  Stats stats{};
  int rc = 0;
  if (!corpus.empty()) {
    rc = run_corpus(corpus, mutations, stats, sandbox);
  } else {
    FILE *input = stdin;
    if (single != nullptr) {
      input = std::fopen(single, "rb");
      if (input == nullptr) {
        return 2;
      }
    }
    std::vector<std::uint8_t> buffer(512U * 1024U);
    const std::size_t n = std::fread(buffer.data(), 1, buffer.size(), input);
    if (input != stdin) {
      std::fclose(input);
    }
    rc = run_once(buffer.data(), n, &stats, Kind::auto_detect, sandbox);
  }
  print_stats(stats);
  fs::remove_all(sandbox, ec);
  return rc;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  std::error_code ec;
  const fs::path sandbox =
      fs::temp_directory_path(ec) / "instantee-fuzz-asset-import-libfuzzer";
  fs::create_directories(sandbox, ec);
  const int rc = run_once(data, size, nullptr, Kind::auto_detect, sandbox);
  fs::remove_all(sandbox, ec);
  return rc;
}
#endif
