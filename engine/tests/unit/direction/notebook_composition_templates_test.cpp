#include <instantee/direction/notebook_composition_templates.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

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
                                    std::string_view contents) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << contents;
  return static_cast<bool>(out);
}

[[nodiscard]] bool check_golden(const std::filesystem::path &path,
                                std::string_view actual, bool write_goldens) {
  if (write_goldens) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << actual;
    return true;
  }
  if (!write_if_missing(path, actual)) {
    return false;
  }
  return read_file(path) == actual;
}

} // namespace

int main(int argc, char **argv) {
  using namespace instantee::direction;

  if (argc < 2) {
    std::cerr << "usage: notebook_composition_templates_test <repo-root> "
                 "[--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  bool write_goldens = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--write-goldens") {
      write_goldens = true;
    }
  }
  const auto example_dir =
      root / "contracts/fixtures/notebook-templates/examples";
  const auto bible_path =
      root /
      "contracts/fixtures/style-bible/positive/scientific-notebook.style.json";

  auto bible = load_style_bible_file(bible_path);
  expect(bible.has_value(), "load notebook style bible");
  if (!bible) {
    return EXIT_FAILURE;
  }

  auto pythag = assemble_notebook_composition_from_style(
      *bible, example_pythagoras_topic());
  expect(pythag.has_value(), "assemble pythagoras");
  if (!pythag) {
    std::cerr << pythag.error().code << ": " << pythag.error().message << '\n';
    return EXIT_FAILURE;
  }

  expect(!pythag->has_audio, "silent templates");
  expect(pythag->layouts.size() == 5, "five layouts");
  expect(pythag->style_package_id == "scientific-notebook", "style package");
  expect(pythag->topic_id == "pythagoras", "topic id");
  expect(pythag->layouts[0].kind == NotebookLayoutKind::problem_statement,
         "problem");
  expect(pythag->layouts[1].kind == NotebookLayoutKind::construction,
         "construction");
  expect(pythag->layouts[2].kind == NotebookLayoutKind::derivation,
         "derivation");
  expect(pythag->layouts[3].kind == NotebookLayoutKind::visual_intuition,
         "intuition");
  expect(pythag->layouts[4].kind == NotebookLayoutKind::summary, "summary");

  expect(notebook_layout_slot(pythag->layouts[0], "prompt") != nullptr,
         "problem prompt slot");
  expect(notebook_layout_slot(pythag->layouts[1], "step-1") != nullptr,
         "construction step");
  expect(notebook_layout_slot(pythag->layouts[2], "eq-1") != nullptr,
         "derivation equation");
  expect(notebook_layout_slot(pythag->layouts[3], "diagram") != nullptr,
         "diagram slot");
  expect(notebook_layout_slot(pythag->layouts[4], "claim") != nullptr,
         "summary claim");

  // Same template + different topic data expresses a new subject.
  auto quadratic = assemble_notebook_composition_from_style(
      *bible, example_quadratic_topic());
  expect(quadratic.has_value(), "assemble quadratic");
  if (!quadratic) {
    std::cerr << quadratic.error().code << ": " << quadratic.error().message
              << '\n';
    return EXIT_FAILURE;
  }
  expect(quadratic->layouts.size() == pythag->layouts.size(),
         "same template skeleton");
  for (std::size_t i = 0; i < pythag->layouts.size(); ++i) {
    expect(quadratic->layouts[i].kind == pythag->layouts[i].kind,
           "layout kinds match");
    expect(quadratic->layouts[i].slots.size() >= 1, "slots present");
  }
  const auto *p_eq = notebook_layout_slot(pythag->layouts[2], "eq-2");
  const auto *q_eq = notebook_layout_slot(quadratic->layouts[2], "eq-2");
  expect(p_eq && q_eq, "second equations");
  if (p_eq && q_eq) {
    expect(p_eq->content != q_eq->content, "topic data changes equation text");
  }
  expect(pythag->topic_id != quadratic->topic_id, "distinct topics");

  // Template examples (goldens).
  {
    const std::string a =
        format_notebook_composition_example("pythagoras", *pythag);
    const std::string b =
        format_notebook_composition_example("quadratic", *quadratic);
    expect(check_golden(example_dir / "pythagoras.txt", a, write_goldens),
           "pythagoras example");
    expect(check_golden(example_dir / "quadratic.txt", b, write_goldens),
           "quadratic example");
  }

  // Validation: missing derivation equations fails closed.
  {
    auto bad = example_pythagoras_topic();
    bad.derivation_equations.clear();
    expect(!assemble_notebook_composition_from_style(*bible, bad).has_value(),
           "reject missing equations");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "notebook_composition_templates_test ok\n";
  return EXIT_SUCCESS;
}
