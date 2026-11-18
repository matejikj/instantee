#include <instantee/compiler/compile.hpp>
#include <instantee/direction/creative_variation.hpp>
#include <instantee/direction/style_bible.hpp>
#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/load.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::compiler::compile_authoring;
using instantee::compiler::CompileOptions;
using instantee::direction::load_style_bible_file;
using instantee::direction::sample_creative_variation;
using instantee::direction::VariationIdentity;
using instantee::scene_ir::AuthoringDocument;
using instantee::scene_ir::AuthoringIntent;
using instantee::scene_ir::AuthoringNode;
using instantee::scene_ir::AuthoringShot;
using instantee::scene_ir::load_authoring_json;
using instantee::scene_ir::LoadOptions;
using instantee::scene_ir::SceneDocument;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

struct Variant {
  const char *id;
  std::uint64_t seed;
  const char *relative_path;
  const char *headline;
};

constexpr Variant kVariants[] = {
    {"markets", 42, "variants/markets.authoring.json",
     "Markets open under pressure"},
    {"rates", 77, "variants/rates.authoring.json", "Yields jumped overnight"},
    {"flows", 101, "variants/flows.authoring.json", "Liquidity dried up fast"},
};

[[nodiscard]] std::string track_fingerprint(const SceneDocument &doc,
                                            std::string_view target_id) {
  std::ostringstream out;
  for (const auto &track : doc.animation_tracks) {
    if (track.target_id != target_id) {
      continue;
    }
    out << track.property << ':';
    for (const auto &kf : track.keyframes) {
      out << kf.frame << '=' << kf.value << ';';
    }
    out << '|';
  }
  return out.str();
}

[[nodiscard]] bool peak_within_unit_interval(const SceneDocument &doc) {
  for (const auto &track : doc.animation_tracks) {
    for (const auto &kf : track.keyframes) {
      if (!std::isfinite(kf.value) || kf.value < 0.0 || kf.value > 1.0) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] std::string find_text(const SceneDocument &doc,
                                    std::string_view node_id) {
  for (const auto &node : doc.nodes) {
    if (node.id != node_id || !node.text_run_id) {
      continue;
    }
    for (const auto &run : doc.text_runs) {
      if (run.id == *node.text_run_id) {
        return run.text;
      }
    }
  }
  return {};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: editorial_multi_seed_test <repo-root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto fixture_root = root / "contracts/fixtures/editorial-multi-seed";
  const auto bible_path =
      root / "contracts/fixtures/style-bible/positive/editorial.style.json";
  const auto catalog_path = fixture_root / "catalog.json";

  expect(std::filesystem::exists(catalog_path), "catalog fixture present");
  expect(std::filesystem::exists(bible_path), "editorial style bible present");

  auto bible = load_style_bible_file(bible_path);
  expect(bible.has_value(), "load editorial style bible");
  if (!bible) {
    return EXIT_FAILURE;
  }
  expect(bible->package_id() == "editorial", "editorial package id");
  expect(bible->version() == "1.0.0", "editorial package version");
  expect(bible->colors().contains("ink") && bible->colors().contains("paper") &&
             bible->colors().contains("accent"),
         "editorial palette tokens present");

  CompileOptions options;
  options.style_bible_path = bible_path.string();

  struct CompiledVariant {
    std::string id;
    std::uint64_t seed = 0;
    SceneDocument document;
    std::string hook_fp;
    std::string claim_fp;
    std::string evidence_fp;
  };
  std::vector<CompiledVariant> compiled;
  compiled.reserve(3);

  for (const Variant &variant : kVariants) {
    const auto path = fixture_root / variant.relative_path;
    expect(std::filesystem::exists(path), variant.id);
    const std::string text = read_file(path);
    auto authoring = load_authoring_json(text, LoadOptions{});
    expect(authoring.has_value(), "load authoring");
    if (!authoring) {
      continue;
    }
    expect(authoring->seed == variant.seed, "authoring seed matches catalog");
    expect(authoring->style == "editorial@1", "editorial style ref");

    auto scene = compile_authoring(*authoring, options);
    expect(scene.has_value(), "compile variant");
    if (!scene) {
      std::cerr << "  compile error: " << scene.error().code << " — "
                << scene.error().message << '\n';
      continue;
    }

    expect(scene->seed == variant.seed, "IR seed preserved");
    expect(!scene->provenance.style_packages.empty() &&
               scene->provenance.style_packages.front().id == "editorial",
           "provenance stays editorial");
    expect(find_text(*scene, "headline") == variant.headline,
           "headline text matches variant");
    expect(peak_within_unit_interval(*scene),
           "keyframes stay inside unit interval rules");
    expect(!scene->animation_tracks.empty(), "seeded tracks emitted");

    // Same seed/text recompile is bit-identical for track fingerprints.
    auto again = compile_authoring(*authoring, options);
    expect(again.has_value(), "recompile");
    if (again) {
      expect(track_fingerprint(*scene, "headline") ==
                 track_fingerprint(*again, "headline"),
             "deterministic hook tracks");
    }

    CompiledVariant row;
    row.id = variant.id;
    row.seed = variant.seed;
    row.hook_fp = track_fingerprint(*scene, "headline");
    row.claim_fp = track_fingerprint(*scene, "subhead");
    row.evidence_fp = track_fingerprint(*scene, "left_label") +
                      track_fingerprint(*scene, "right_label");
    row.document = std::move(*scene);
    expect(!row.hook_fp.empty() && !row.claim_fp.empty() &&
               !row.evidence_fp.empty(),
           "per-shot fingerprints");
    compiled.push_back(std::move(row));
  }

  expect(compiled.size() == 3, "three compiled variants");
  if (compiled.size() == 3) {
    // Distinct seeds must remix motion; texts already differ by construction.
    expect(compiled[0].hook_fp != compiled[1].hook_fp ||
               compiled[0].claim_fp != compiled[1].claim_fp ||
               compiled[0].evidence_fp != compiled[1].evidence_fp,
           "markets vs rates motion differs");
    expect(compiled[0].hook_fp != compiled[2].hook_fp ||
               compiled[0].claim_fp != compiled[2].claim_fp ||
               compiled[0].evidence_fp != compiled[2].evidence_fp,
           "markets vs flows motion differs");
    expect(compiled[1].hook_fp != compiled[2].hook_fp ||
               compiled[1].claim_fp != compiled[2].claim_fp ||
               compiled[1].evidence_fp != compiled[2].evidence_fp,
           "rates vs flows motion differs");

    // Palette consistency: Style Bible color tokens are shared; variants do
    // not invent alternate package ids or material shader forks.
    expect(compiled[0].document.materials.size() ==
                   compiled[1].document.materials.size() &&
               compiled[1].document.materials.size() ==
                   compiled[2].document.materials.size(),
           "shared material count");
    for (const auto &row : compiled) {
      expect(row.document.provenance.style_packages.front().id == "editorial",
             "palette package locked");
    }
  }

  // Adding an unrelated shot must not change existing scene tracks.
  {
    const auto path = fixture_root / "variants/markets.authoring.json";
    auto authoring = load_authoring_json(read_file(path), LoadOptions{});
    expect(authoring.has_value(), "reload markets for isolation");
    if (authoring) {
      auto baseline = compile_authoring(*authoring, options);
      expect(baseline.has_value(), "baseline compile");

      AuthoringShot extra;
      extra.id = "extra_insert";
      extra.duration_frames = 60;
      AuthoringIntent intent;
      intent.type = "explain";
      intent.intensity = 0.6;
      intent.importance = 0.5;
      intent.target_ids = {"decoy"};
      extra.intent = intent;
      AuthoringNode decoy;
      decoy.id = "decoy";
      decoy.type = "text";
      decoy.text = "Unrelated insert";
      decoy.role = "caption";
      decoy.visible = true;
      extra.nodes.push_back(std::move(decoy));
      authoring->shots.push_back(std::move(extra));

      auto with_extra = compile_authoring(*authoring, options);
      expect(with_extra.has_value(), "compile with inserted shot");
      if (baseline && with_extra) {
        expect(track_fingerprint(*baseline, "headline") ==
                   track_fingerprint(*with_extra, "headline"),
               "hook unchanged after insert");
        expect(track_fingerprint(*baseline, "subhead") ==
                   track_fingerprint(*with_extra, "subhead"),
               "claim unchanged after insert");
        expect(track_fingerprint(*baseline, "left_label") ==
                   track_fingerprint(*with_extra, "left_label"),
               "evidence left unchanged after insert");
        expect(track_fingerprint(*baseline, "right_label") ==
                   track_fingerprint(*with_extra, "right_label"),
               "evidence right unchanged after insert");
        expect(!track_fingerprint(*with_extra, "decoy").empty(),
               "inserted shot has its own tracks");
      }

      // Direct creative-variation identity check mirrors the compile contract.
      VariationIdentity before;
      before.film_seed = 42;
      before.package_id = "editorial";
      before.package_version = "1.0.0";
      before.shot_id = "hook";
      before.object_id = "headline";
      auto sample_a = sample_creative_variation(before);
      before.shot_id = "extra_insert";
      before.object_id = "decoy";
      auto sample_extra = sample_creative_variation(before);
      before.shot_id = "hook";
      before.object_id = "headline";
      auto sample_b = sample_creative_variation(before);
      expect(sample_a && sample_extra && sample_b, "variation samples");
      if (sample_a && sample_b) {
        expect(sample_a->object_subseed == sample_b->object_subseed,
               "unrelated shot leaves object subseed unchanged");
        expect(std::memcmp(&sample_a->intensity_scale,
                           &sample_b->intensity_scale, sizeof(double)) == 0,
               "intensity bits unchanged");
      }
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "editorial_multi_seed_test: ok\n";
  return EXIT_SUCCESS;
}
