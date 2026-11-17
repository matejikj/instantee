#include <instantee/direction/directed_film.hpp>
#include <instantee/direction/motion_grammar.hpp>
#include <instantee/direction/semantic_intent.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using instantee::direction::IntentKind;
using instantee::direction::lower_semantic_intent;
using instantee::direction::make_builtin_motion_grammar_registry;
using instantee::direction::make_semantic_intent;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string
fingerprint(const std::vector<instantee::direction::DirectedTrack> &tracks) {
  std::string out;
  for (const auto &track : tracks) {
    out.append(track.target_id);
    out.push_back('|');
    out.append(track.property);
    out.push_back('|');
    for (const auto &kf : track.keyframes) {
      out.append(std::to_string(kf.frame));
      out.push_back(':');
      out.append(std::to_string(kf.value));
      out.push_back(';');
    }
    out.push_back('#');
  }
  return out;
}

} // namespace

int main() {
  auto grammar = make_builtin_motion_grammar_registry();
  expect(grammar.contains("editorial", IntentKind::reveal), "editorial reveal");
  expect(grammar.contains("paper-cinema", IntentKind::reveal), "paper reveal");
  expect(grammar.contains("scientific-notebook", IntentKind::reveal),
         "notebook reveal");

  const std::array<std::string_view, 2> known{"a", "b"};
  auto intent = make_semantic_intent(IntentKind::reveal,
                                     std::array<std::string_view, 2>{"a", "b"},
                                     0.8, 0.9, std::nullopt, known);
  expect(intent.has_value(), "valid reveal intent");
  if (!intent) {
    return EXIT_FAILURE;
  }

  auto editorial =
      lower_semantic_intent(*intent, 60, "s0", "editorial", grammar);
  auto paper =
      lower_semantic_intent(*intent, 60, "s0", "paper-cinema", grammar);
  auto notebook =
      lower_semantic_intent(*intent, 60, "s0", "scientific-notebook", grammar);
  expect(editorial && paper && notebook, "lower three styles");
  if (!editorial || !paper || !notebook) {
    return EXIT_FAILURE;
  }

  const auto fe = fingerprint(*editorial);
  const auto fp = fingerprint(*paper);
  const auto fn = fingerprint(*notebook);
  expect(fe != fp && fe != fn && fp != fn,
         "same intent yields different style tracks");

  auto editorial_again =
      lower_semantic_intent(*intent, 60, "s0", "editorial", grammar);
  expect(editorial_again && fingerprint(*editorial_again) == fe,
         "editorial deterministic");
  auto paper_again =
      lower_semantic_intent(*intent, 60, "s0", "paper-cinema", grammar);
  expect(paper_again && fingerprint(*paper_again) == fp, "paper deterministic");

  // Stagger pattern differs: editorial stagger 2 vs paper 4 on compare.
  auto compare = make_semantic_intent(IntentKind::compare,
                                      std::array<std::string_view, 2>{"a", "b"},
                                      0.7, 0.5, std::nullopt, known);
  expect(compare.has_value(), "compare intent");
  if (compare) {
    auto c_ed = lower_semantic_intent(*compare, 90, "s1", "editorial", grammar);
    auto c_paper =
        lower_semantic_intent(*compare, 90, "s1", "paper-cinema", grammar);
    expect(c_ed && c_paper, "compare lowers");
    if (c_ed && c_paper) {
      expect(c_ed->size() == 2 && c_paper->size() == 2, "two targets");
      expect(c_ed->at(1).keyframes.front().frame !=
                 c_paper->at(1).keyframes.front().frame,
             "stagger frames differ by style");
    }
  }

  auto missing =
      lower_semantic_intent(*intent, 60, "s0", "unknown-style", grammar);
  expect(!missing.has_value(), "unknown package grammar fails");
  if (!missing) {
    expect(missing.error().code == "unknown_motion_grammar",
           "unknown grammar code");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
