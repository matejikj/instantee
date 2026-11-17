#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/migrate.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

} // namespace

int main() {
  const std::filesystem::path root = INSTANTEE_SOURCE_DIR;
  const auto input_path =
      root /
      "contracts/fixtures/authoring/migrations/from-0.9.0/input.authoring.json";
  const auto expected_path =
      root / "contracts/fixtures/authoring/migrations/from-0.9.0/"
             "expected.authoring.json";
  const std::string input = read_file(input_path);
  const std::string expected_text = read_file(expected_path);

  // Purity: keep a copy and ensure migrate does not alter the original buffer.
  std::vector<std::byte> owned(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    owned[i] = static_cast<std::byte>(static_cast<unsigned char>(input[i]));
  }
  const std::string before(reinterpret_cast<const char *>(owned.data()),
                           owned.size());
  auto migrated = instantee::scene_ir::migrate_authoring_json(owned);
  expect(migrated.has_value(), "migrate legacy 0.9.0");
  const std::string after(reinterpret_cast<const char *>(owned.data()),
                          owned.size());
  expect(before == after, "input buffer unchanged");

  instantee::scene_ir::LoadOptions options;
  auto expected_doc =
      instantee::scene_ir::load_authoring_json(expected_text, options);
  auto migrated_doc =
      instantee::scene_ir::load_authoring_json(*migrated, options);
  expect(expected_doc.has_value() && migrated_doc.has_value(),
         "load expected and migrated");
  if (expected_doc && migrated_doc) {
    expect(migrated_doc->schema_version == "1.0.0", "schema bumped");
    expect(migrated_doc->style == "editorial@1", "style renamed");
    expect(migrated_doc->film_id == expected_doc->film_id, "film_id preserved");
    expect(migrated_doc->seed == expected_doc->seed, "seed preserved");
    expect(migrated_doc->shots.size() == expected_doc->shots.size(),
           "shot count");
  }

  // Loading the legacy fixture through the public loader migrates
  // transparently.
  auto via_loader = instantee::scene_ir::load_authoring_json(input, options);
  expect(via_loader.has_value(), "loader migrates legacy");
  if (via_loader) {
    expect(via_loader->style == "editorial@1", "loader style");
  }

  const char *unsupported = R"({"schema_version":"2.0.0","film_id":"x"})";
  const auto *bytes = reinterpret_cast<const std::byte *>(unsupported);
  auto rejected =
      instantee::scene_ir::migrate_authoring_json(std::span<const std::byte>(
          bytes, std::char_traits<char>::length(unsupported)));
  expect(!rejected.has_value(), "reject unsupported major");
  if (!rejected) {
    expect(rejected.error().code == "unsupported_schema",
           "unsupported_schema code");
    expect(rejected.error().exit_code ==
               instantee::diagnostics::ExitCode::incompatible_schema,
           "exit incompatible_schema");
  }

  auto current = instantee::scene_ir::parse_schema_semver("1.0.0");
  expect(current.has_value(), "parse 1.0.0");
  if (current) {
    expect(current->major == 1 && current->minor == 0 && current->patch == 0,
           "semver fields");
  }
  expect(!instantee::scene_ir::parse_schema_semver("01.0.0"),
         "reject leading zero");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
