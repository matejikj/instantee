#include <instantee/core_model/time.hpp>
#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// Bounded fuzz driver for Scene IR / authoring JSON. Caps input size to
// InputLimits, applies a fixed deterministic mutation schedule, and exercises
// parse, structural validation, graph checks, and frame arithmetic. Failures
// return Error; the harness must not throw, hang, or allocate unboundedly.
namespace {

namespace fs = std::filesystem;

struct Stats {
  std::uint64_t inputs = 0;
  std::uint64_t scene_ok = 0;
  std::uint64_t scene_err = 0;
  std::uint64_t authoring_ok = 0;
  std::uint64_t authoring_err = 0;
  std::uint64_t graph_ok = 0;
  std::uint64_t graph_err = 0;
  std::uint64_t frame_ops = 0;
};

void exercise_frame_arithmetic(std::uint64_t duration_frames,
                               Stats &stats) noexcept {
  using instantee::core::FrameClock;
  using instantee::core::FrameIndex;

  (void)FrameIndex::checked_add(FrameIndex{duration_frames}, 1U);
  ++stats.frame_ops;
  (void)FrameIndex::checked_add(FrameIndex::max(), 1U);
  ++stats.frame_ops;

  const auto clock = FrameClock::media_30_1();
  const FrameIndex at{duration_frames == 0U ? 0U : duration_frames - 1U};
  (void)clock.pts(at);
  ++stats.frame_ops;
  (void)clock.duration(FrameIndex{duration_frames});
  ++stats.frame_ops;
  (void)clock.pts(FrameIndex::max());
  ++stats.frame_ops;
}

void exercise_authoring_node_frames(
    const instantee::scene_ir::AuthoringNode &node, Stats &stats) noexcept {
  using instantee::core::FrameIndex;
  for (const auto &animation : node.animations) {
    for (const auto &key : animation.keyframes) {
      (void)FrameIndex::checked_add(FrameIndex{key.frame}, 1U);
      ++stats.frame_ops;
    }
  }
  for (const auto &child : node.children) {
    exercise_authoring_node_frames(child, stats);
  }
}

void exercise_authoring_frames(
    const instantee::scene_ir::AuthoringDocument &doc, Stats &stats) noexcept {
  using instantee::core::FrameIndex;
  std::uint64_t cursor = 0;
  for (const auto &shot : doc.shots) {
    auto next =
        FrameIndex::checked_add(FrameIndex{cursor}, shot.duration_frames);
    if (next) {
      cursor = next->value();
    }
    ++stats.frame_ops;
    for (const auto &node : shot.nodes) {
      exercise_authoring_node_frames(node, stats);
    }
  }
  exercise_frame_arithmetic(cursor, stats);
}

int run_once(const std::uint8_t *data, std::size_t size, Stats *stats) {
  instantee::scene_ir::LoadOptions options;
  if (size > options.limits.max_bytes) {
    size = options.limits.max_bytes;
  }
  // Keep pathological nesting cheap under fuzz: still reject via limits.
  options.limits.max_depth =
      std::min(options.limits.max_depth, static_cast<std::uint32_t>(32));
  options.limits.max_values =
      std::min(options.limits.max_values, static_cast<std::uint32_t>(20000));

  const auto *bytes = reinterpret_cast<const std::byte *>(data);
  const std::span<const std::byte> span(bytes, size);

  if (stats != nullptr) {
    ++stats->inputs;
  }

  const auto loaded = instantee::scene_ir::load_scene_json(span, options);
  if (loaded) {
    if (stats != nullptr) {
      ++stats->scene_ok;
    }
    const auto graph = instantee::scene_ir::validate_graph(*loaded);
    if (stats != nullptr) {
      if (graph) {
        ++stats->graph_ok;
      } else {
        ++stats->graph_err;
      }
    }
    if (stats != nullptr) {
      exercise_frame_arithmetic(loaded->duration_frames.value(), *stats);
    } else {
      Stats discard{};
      exercise_frame_arithmetic(loaded->duration_frames.value(), discard);
    }
  } else if (stats != nullptr) {
    ++stats->scene_err;
  }

  const auto authoring =
      instantee::scene_ir::load_authoring_json(span, options);
  if (authoring) {
    if (stats != nullptr) {
      ++stats->authoring_ok;
      exercise_authoring_frames(*authoring, *stats);
    } else {
      Stats discard{};
      exercise_authoring_frames(*authoring, discard);
    }
  } else if (stats != nullptr) {
    ++stats->authoring_err;
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
    const auto ext = entry.path().extension().string();
    if (ext == ".json" || ext == ".seed" || ext == ".bin") {
      out.push_back(entry.path());
    }
  }
  std::sort(out.begin(), out.end());
}

// Deterministic, bounded mutations. No random device; seed = index.
[[nodiscard]] std::vector<std::uint8_t> mutate(std::vector<std::uint8_t> input,
                                               std::uint32_t which,
                                               std::size_t max_bytes) {
  if (input.size() > max_bytes) {
    input.resize(max_bytes);
  }
  switch (which % 12U) {
  case 0:
    return input;
  case 1:
    if (!input.empty()) {
      input[which % input.size()] ^= static_cast<std::uint8_t>(0xA5U + which);
    }
    return input;
  case 2:
    if (input.size() > 1U) {
      input.resize(input.size() / 2U);
    }
    return input;
  case 3:
    input.insert(input.end(), {'}', ']', 0x00, 0xFF});
    if (input.size() > max_bytes) {
      input.resize(max_bytes);
    }
    return input;
  case 4: {
    std::vector<std::uint8_t> deep;
    deep.reserve(96);
    deep.insert(deep.end(), 40, static_cast<std::uint8_t>('['));
    deep.insert(deep.end(), 40, static_cast<std::uint8_t>(']'));
    return deep;
  }
  case 5:
    return std::vector<std::uint8_t>(std::min<std::size_t>(max_bytes, 4096),
                                     static_cast<std::uint8_t>('x'));
  case 6: {
    const char *payload =
        R"({"schema_version":"1.0.0","duration_frames":18446744073709551615})";
    return std::vector<std::uint8_t>(payload, payload + std::strlen(payload));
  }
  case 7: {
    const char *payload =
        R"({"schema_version":"1.0.0","shots":[{"id":"a","start_frame":10,"duration_frames":1},{"id":"b","start_frame":0,"duration_frames":1}]})";
    return std::vector<std::uint8_t>(payload, payload + std::strlen(payload));
  }
  case 8: {
    // Cyclic parent hint for graph validation once structure parses.
    const char *payload =
        R"({"schema_version":"1.0.0","compiler_version":"0.1.0","canvas":{"width":8,"height":8,"color":"rec709-sdr"},"frame_rate":{"numerator":30,"denominator":1},"duration_frames":1,"seed":1,"assets":[],"materials":[],"geometry":[],"text_runs":[],"cameras":[],"rigs":[],"nodes":[{"id":"a","kind":"group","layer_order":0,"visible":true,"children":["b"]},{"id":"b","kind":"group","layer_order":1,"visible":true,"children":["a"]}],"animation_tracks":[],"shots":[{"id":"s","start_frame":0,"duration_frames":1,"node_ids":["a"],"camera_node_id":"a"}],"transition_spans":[],"provenance":{"project_sha256":"0000000000000000000000000000000000000000000000000000000000000000","style_packages":[],"source_schema_version":"1.0.0"}})";
    return std::vector<std::uint8_t>(payload, payload + std::strlen(payload));
  }
  case 9:
    input.push_back(static_cast<std::uint8_t>(which));
    if (input.size() > max_bytes) {
      input.resize(max_bytes);
    }
    return input;
  case 10:
    return {};
  default:
    if (!input.empty()) {
      const auto mid = input.size() / 2U;
      input.insert(input.begin() + static_cast<std::ptrdiff_t>(mid), '{');
    }
    if (input.size() > max_bytes) {
      input.resize(max_bytes);
    }
    return input;
  }
}

int run_corpus(const fs::path &corpus_root, const fs::path &fixtures_root,
               std::uint32_t mutations, Stats &stats) {
  std::vector<fs::path> seeds;
  collect_seeds(corpus_root, seeds);
  if (!fixtures_root.empty()) {
    collect_seeds(fixtures_root / "scene-ir", seeds);
    collect_seeds(fixtures_root / "authoring", seeds);
  }
  if (seeds.empty()) {
    std::cerr << "fuzz: no corpus seeds found\n";
    return 2;
  }

  const std::size_t max_bytes = instantee::scene_ir::InputLimits{}.max_bytes;
  for (const auto &seed_path : seeds) {
    auto base = read_file(seed_path);
    if (base.empty() && fs::file_size(seed_path) != 0U) {
      std::cerr << "fuzz: failed to read " << seed_path << '\n';
      return 2;
    }
    const std::uint32_t rounds = std::max<std::uint32_t>(1U, mutations);
    for (std::uint32_t i = 0; i < rounds; ++i) {
      auto sample = mutate(base, i, max_bytes);
      (void)run_once(sample.data(), sample.size(), &stats);
    }
  }
  return 0;
}

void print_stats(const Stats &stats) {
  std::cout << "fuzz_report"
            << " inputs=" << stats.inputs << " scene_ok=" << stats.scene_ok
            << " scene_err=" << stats.scene_err
            << " authoring_ok=" << stats.authoring_ok
            << " authoring_err=" << stats.authoring_err
            << " graph_ok=" << stats.graph_ok
            << " graph_err=" << stats.graph_err
            << " frame_ops=" << stats.frame_ops << '\n';
}

} // namespace

#if defined(INSTANTEE_FUZZ_STANDALONE)
int main(int argc, char **argv) {
  fs::path corpus;
  fs::path fixtures;
  std::uint32_t mutations = 8;
  const char *single = nullptr;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--corpus" && i + 1 < argc) {
      corpus = argv[++i];
    } else if (arg == "--fixtures" && i + 1 < argc) {
      fixtures = argv[++i];
    } else if (arg == "--mutations" && i + 1 < argc) {
      mutations =
          static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
      if (mutations == 0U) {
        mutations = 1U;
      }
      if (mutations > 256U) {
        mutations = 256U;
      }
    } else if (!arg.empty() && arg[0] != '-') {
      single = argv[i];
    } else {
      std::cerr << "usage: fuzz-scene-ir-smoke [--corpus DIR] [--fixtures DIR] "
                   "[--mutations N] [file]\n";
      return 2;
    }
  }

  Stats stats{};
  if (!corpus.empty() || !fixtures.empty()) {
    if (const int rc = run_corpus(corpus, fixtures, mutations, stats)) {
      return rc;
    }
    print_stats(stats);
    return 0;
  }

  FILE *input = stdin;
  if (single != nullptr) {
    input = std::fopen(single, "rb");
    if (input == nullptr) {
      return 2;
    }
  }
  std::vector<std::uint8_t> buffer(
      instantee::scene_ir::InputLimits{}.max_bytes);
  const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), input);
  if (input != stdin) {
    std::fclose(input);
  }
  const int rc = run_once(buffer.data(), read, &stats);
  print_stats(stats);
  return rc;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  return run_once(data, size, nullptr);
}
#endif
