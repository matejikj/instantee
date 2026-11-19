#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/migrate.hpp>

#include <yyjson.h>

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

void expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

struct DocGuard {
  yyjson_doc *doc = nullptr;
  ~DocGuard() { yyjson_doc_free(doc); }
  DocGuard(const DocGuard &) = delete;
  DocGuard &operator=(const DocGuard &) = delete;
  explicit DocGuard(yyjson_doc *owned) : doc(owned) {}
};

std::string_view require_str(yyjson_val *obj, const char *key) {
  yyjson_val *value = yyjson_obj_get(obj, key);
  if (value == nullptr || !yyjson_is_str(value)) {
    return {};
  }
  return {yyjson_get_str(value), yyjson_get_len(value)};
}

} // namespace

int main() {
  const std::filesystem::path root = INSTANTEE_SOURCE_DIR;
  const auto catalog_path =
      root / "contracts/compatibility/locked-examples-v1.json";
  const std::string catalog_text = read_file(catalog_path);
  DocGuard catalog{yyjson_read(catalog_text.data(), catalog_text.size(), 0)};
  expect(catalog.doc != nullptr, "parse locked-examples catalog");
  if (catalog.doc == nullptr) {
    return EXIT_FAILURE;
  }
  yyjson_val *examples =
      yyjson_obj_get(yyjson_doc_get_root(catalog.doc), "examples");
  expect(examples != nullptr && yyjson_is_arr(examples), "examples array");
  if (examples == nullptr || !yyjson_is_arr(examples)) {
    return EXIT_FAILURE;
  }

  instantee::scene_ir::LoadOptions options;
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(examples, &iter);
  yyjson_val *entry = nullptr;
  std::size_t covered = 0;
  while ((entry = yyjson_arr_iter_next(&iter)) != nullptr) {
    const auto id = std::string(require_str(entry, "id"));
    const auto rel = std::string(require_str(entry, "path"));
    const auto family = std::string(require_str(entry, "family"));
    const auto expect_kind = std::string(require_str(entry, "expect"));
    expect(!id.empty() && !rel.empty() && !family.empty() &&
               !expect_kind.empty(),
           "catalog entry fields");
    const auto path = root / rel;
    expect(std::filesystem::exists(path), "missing fixture " + rel);
    const std::string text = read_file(path);
    ++covered;

    if (family == "authoring") {
      if (expect_kind == "load" || expect_kind == "migrate_and_load") {
        if (expect_kind == "migrate_and_load") {
          const auto from = require_str(entry, "from_version");
          auto support =
              instantee::scene_ir::classify_authoring_schema_version(from);
          expect(support.has_value() &&
                     *support ==
                         instantee::scene_ir::SchemaVersionSupport::migratable,
                 id + " from_version must be migratable");
        }
        auto loaded = instantee::scene_ir::load_authoring_json(text, options);
        expect(loaded.has_value(), id + " should load");
        if (loaded) {
          expect(loaded->schema_version == "1.0.0", id + " current schema");
        }
      } else if (expect_kind == "unsupported_schema") {
        auto loaded = instantee::scene_ir::load_authoring_json(text, options);
        expect(!loaded.has_value(), id + " should reject");
        if (!loaded) {
          expect(loaded.error().code == "unsupported_schema",
                 id + " unsupported_schema code");
          expect(loaded.error().exit_code ==
                     instantee::diagnostics::ExitCode::incompatible_schema,
                 id + " incompatible_schema exit");
        }
      } else if (expect_kind == "unknown_field") {
        auto loaded = instantee::scene_ir::load_authoring_json(text, options);
        expect(!loaded.has_value(), id + " should reject unknown field");
        if (!loaded) {
          expect(loaded.error().code == "unknown_field",
                 id + " unknown_field code");
        }
      } else {
        expect(false, id + " unknown expect kind");
      }
    } else if (family == "scene-ir") {
      if (expect_kind == "load") {
        auto loaded = instantee::scene_ir::load_scene_json(text, options);
        expect(loaded.has_value(), id + " scene should load");
        if (loaded) {
          expect(loaded->schema_version == "1.0.0", id + " scene schema");
        }
      } else if (expect_kind == "unsupported_schema") {
        auto loaded = instantee::scene_ir::load_scene_json(text, options);
        expect(!loaded.has_value(), id + " scene should reject");
        if (!loaded) {
          expect(loaded.error().code == "unsupported_schema",
                 id + " scene unsupported_schema");
        }
      } else {
        expect(false, id + " unsupported scene expect");
      }
    } else {
      expect(false, id + " unsupported family in C++ suite");
    }
  }

  expect(covered >= 8, "catalog must cover locked examples");

  // Matrix spot-checks for authoring classification.
  auto current =
      instantee::scene_ir::classify_authoring_schema_version("1.0.0");
  expect(current &&
             *current == instantee::scene_ir::SchemaVersionSupport::current,
         "classify current");
  auto legacy = instantee::scene_ir::classify_authoring_schema_version("0.9.0");
  expect(legacy &&
             *legacy == instantee::scene_ir::SchemaVersionSupport::migratable,
         "classify migratable");
  auto old = instantee::scene_ir::classify_authoring_schema_version("0.8.0");
  expect(old && *old == instantee::scene_ir::SchemaVersionSupport::unsupported,
         "classify unsupported minor");
  auto major = instantee::scene_ir::classify_authoring_schema_version("2.0.0");
  expect(major &&
             *major == instantee::scene_ir::SchemaVersionSupport::unsupported,
         "classify unsupported major");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "schema compatibility suite passed (" << covered
            << " locked examples)\n";
  return EXIT_SUCCESS;
}
