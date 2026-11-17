#include <instantee/compiler/compile.hpp>
#include <instantee/direction/style_bible.hpp>
#include <instantee/direction/style_package_registry.hpp>
#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/load.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using instantee::compiler::compile_authoring;
using instantee::compiler::CompileOptions;
using instantee::compiler::scene_document_to_json;
using instantee::direction::EngineCapabilities;
using instantee::direction::load_style_bible_file;
using instantee::direction::make_builtin_style_package_registry;
using instantee::direction::make_default_engine_capabilities;
using instantee::direction::StylePackageDesc;
using instantee::scene_ir::AuthoringDocument;
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

[[nodiscard]] std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char ch : text) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
      out.push_back(static_cast<char>(ch));
    } else if (ch < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
      out.append(buf);
    } else {
      out.push_back(static_cast<char>(ch));
    }
  }
  return out;
}

// Compact, deterministic track summary used as a multi-style golden.
[[nodiscard]] std::string tracks_golden(const SceneDocument &doc,
                                        std::string_view package_id,
                                        std::string_view package_version) {
  struct Row {
    std::string target_id;
    std::string property;
    std::vector<std::pair<std::uint64_t, double>> keyframes;
  };
  std::vector<Row> rows;
  rows.reserve(doc.animation_tracks.size());
  for (const auto &track : doc.animation_tracks) {
    Row row;
    row.target_id = track.target_id;
    row.property = track.property;
    row.keyframes.reserve(track.keyframes.size());
    for (const auto &kf : track.keyframes) {
      row.keyframes.push_back({kf.frame, kf.value});
    }
    rows.push_back(std::move(row));
  }
  std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
    if (a.target_id != b.target_id) {
      return a.target_id < b.target_id;
    }
    if (a.property != b.property) {
      return a.property < b.property;
    }
    return a.keyframes < b.keyframes;
  });

  std::ostringstream out;
  out << "{\n  \"schema_version\": \"multi-style-tracks/1\",\n  "
         "\"package_id\": \""
      << json_escape(package_id) << "\",\n  \"package_version\": \""
      << json_escape(package_version) << "\",\n  \"tracks\": [";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << "\n    {\n      \"target_id\": \"" << json_escape(rows[i].target_id)
        << "\",\n      \"property\": \"" << json_escape(rows[i].property)
        << "\",\n      \"keyframes\": [";
    for (std::size_t k = 0; k < rows[i].keyframes.size(); ++k) {
      if (k != 0) {
        out << ", ";
      }
      out << "{\"frame\":" << rows[i].keyframes[k].first
          << ",\"value\":" << rows[i].keyframes[k].second << '}';
    }
    out << "]\n    }";
  }
  out << "\n  ]\n}\n";
  return out.str();
}

bool write_if_missing(const std::filesystem::path &path,
                      const std::string &body) {
  if (std::filesystem::exists(path)) {
    return false;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << body;
  return static_cast<bool>(out);
}

[[nodiscard]] std::expected<SceneDocument, instantee::diagnostics::Error>
compile_with_style(AuthoringDocument doc, std::string style) {
  doc.style = std::move(style);
  CompileOptions options;
  return compile_authoring(doc, options);
}

} // namespace

int main(int argc, char **argv) {
  const bool write_goldens =
      argc == 2 && std::string_view(argv[1]) == "--write-goldens";
  if (argc != 1 && !write_goldens) {
    std::cerr << "usage: multi_style_compile_test [--write-goldens]\n";
    return EXIT_FAILURE;
  }

  const std::filesystem::path root = INSTANTEE_SOURCE_DIR;
  const auto editorial_bible =
      root / "contracts/fixtures/style-bible/positive/editorial.style.json";
  const auto paper_bible =
      root / "contracts/fixtures/style-bible/positive/paper-cinema.style.json";
  const auto notebook_bible = root / "contracts/fixtures/style-bible/positive/"
                                     "scientific-notebook.style.json";
  const auto neutral_path =
      root / "contracts/fixtures/multi-style/neutral.authoring.json";

  expect(std::filesystem::exists(editorial_bible), "editorial skeleton");
  expect(std::filesystem::exists(paper_bible), "paper skeleton");
  expect(std::filesystem::exists(notebook_bible), "notebook skeleton");
  expect(std::filesystem::exists(neutral_path), "neutral authoring");

  auto editorial = load_style_bible_file(editorial_bible);
  auto paper = load_style_bible_file(paper_bible);
  auto notebook = load_style_bible_file(notebook_bible);
  expect(editorial && editorial->package_id() == "editorial", "load editorial");
  expect(paper && paper->package_id() == "paper-cinema", "load paper");
  expect(notebook && notebook->package_id() == "scientific-notebook",
         "load notebook");
  expect(editorial && paper && notebook &&
             editorial->package_id() != paper->package_id() &&
             editorial->package_id() != notebook->package_id() &&
             paper->package_id() != notebook->package_id(),
         "three distinct package ids");

  const std::string neutral_text = read_file(neutral_path);
  LoadOptions load_opts;
  auto authoring = load_authoring_json(neutral_text, load_opts);
  expect(authoring.has_value(), "load neutral authoring");
  if (!authoring) {
    return EXIT_FAILURE;
  }

  struct Case {
    const char *style_ref;
    const char *package_id;
    const char *golden_name;
  };
  const Case cases[] = {
      {"editorial@1", "editorial", "editorial.tracks.json"},
      {"scientific-notebook@1", "scientific-notebook",
       "scientific-notebook.tracks.json"},
      {"paper-cinema@1", "paper-cinema", "paper-cinema.tracks.json"},
  };

  std::vector<std::string> goldens;
  goldens.reserve(3);
  for (const Case &c : cases) {
    auto compiled = compile_with_style(*authoring, c.style_ref);
    expect(compiled.has_value(), c.style_ref);
    if (!compiled) {
      std::cerr << "  compile error: " << compiled.error().code << " — "
                << compiled.error().message << '\n';
      continue;
    }
    expect(!compiled->animation_tracks.empty(), "tracks emitted");
    expect(compiled->provenance.style_packages.size() == 1 &&
               compiled->provenance.style_packages.front().id == c.package_id,
           "provenance style package");
    // No unresolved semantic intents remain in IR JSON.
    auto json = scene_document_to_json(*compiled);
    expect(json.has_value() && json->find("\"intent\"") == std::string::npos,
           "no unresolved intent");

    const std::string golden = tracks_golden(*compiled, c.package_id, "1.0.0");
    const auto golden_path =
        root / "contracts/fixtures/multi-style/goldens" / c.golden_name;
    if (write_goldens) {
      std::filesystem::create_directories(golden_path.parent_path());
      std::ofstream out(golden_path, std::ios::binary | std::ios::trunc);
      out << golden;
      expect(static_cast<bool>(out), "write golden");
    } else if (!std::filesystem::exists(golden_path)) {
      expect(write_if_missing(golden_path, golden), "seed missing golden");
    }
    expect(std::filesystem::exists(golden_path), "golden present");
    if (std::filesystem::exists(golden_path)) {
      const std::string expected = read_file(golden_path);
      expect(expected == golden, c.golden_name);
    }
    goldens.push_back(golden);

    // Deterministic recompile for the same style.
    auto again = compile_with_style(*authoring, c.style_ref);
    expect(again.has_value() &&
               tracks_golden(*again, c.package_id, "1.0.0") == golden,
           "deterministic style compile");
  }

  expect(goldens.size() == 3, "three goldens");
  if (goldens.size() == 3) {
    expect(goldens[0] != goldens[1] && goldens[0] != goldens[2] &&
               goldens[1] != goldens[2],
           "three distinct direction plans");
  }

  // Fourth synthetic style package: registry/capabilities only — no core-model
  // or render-core change required.
  {
    auto registry = make_builtin_style_package_registry();
    expect(registry.package_ids().size() == 3,
           "three builtins before extension");
    StylePackageDesc poster{
        .package_id = "synthetic-poster",
        .version = "1.0.0",
        .minimum_engine_version = "0.1.0",
        .required_capabilities = {"path_geometry", "text", "camera", "export"},
        .registered_components = {"poster_masthead", "poster_rule"},
        .shader_features = {"solid-fill"},
        .material_presets = {"flat_ink"},
    };
    auto registered = registry.register_package(std::move(poster));
    expect(registered.has_value(), "register synthetic fourth package");
    expect(registry.package_ids().size() == 4, "four packages after extension");
    const EngineCapabilities engine = make_default_engine_capabilities();
    auto ready =
        registry.check_before_render("synthetic-poster", "1.0.0", engine);
    expect(ready.has_value(), "synthetic package ready before render");
    // Builtin compile path still ignores unregistered motion grammar for the
    // synthetic id — extension is registry/capabilities only for this delivery.
    expect(!registry.contains("synthetic-poster", "2.0.0"),
           "unknown synthetic revision stays rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
