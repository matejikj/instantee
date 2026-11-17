#include <instantee/compiler/compile.hpp>
#include <instantee/scene_ir/authoring.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::scene_ir;
  instantee::compiler::CompileOptions options;

  // Author opacity animation + reveal intent on the same target → hard
  // conflict.
  AuthoringDocument doc;
  doc.schema_version = "1.0.0";
  doc.film_id = "conflict";
  doc.canvas_width = 1080;
  doc.canvas_height = 1920;
  doc.frame_rate = instantee::core::FrameRate::media_30_1();
  doc.seed = 7;
  doc.style = "editorial@1";

  AuthoringShot shot;
  shot.id = "s0";
  shot.duration_frames = 30;
  AuthoringIntent intent;
  intent.type = "reveal";
  intent.intensity = 0.8;
  intent.importance = 0.9;
  intent.target_ids = {"headline"};
  shot.intent = intent;

  AuthoringNode node;
  node.id = "headline";
  node.type = "text";
  node.text = "TECH";
  AuthoringAnimation animation;
  animation.property = "opacity";
  animation.keyframes = {AuthoringKeyframe{.frame = 0, .value = 1.0},
                         AuthoringKeyframe{.frame = 29, .value = 1.0}};
  node.animations.push_back(std::move(animation));
  shot.nodes.push_back(std::move(node));
  doc.shots.push_back(std::move(shot));

  auto rejected = instantee::compiler::compile_authoring(doc, options);
  expect(!rejected.has_value(), "compile rejects author/CDS overwrite");
  if (!rejected) {
    expect(rejected.error().code == "hard_direction_conflict",
           "hard_direction_conflict code");
  }

  // Author-only animation (no intent) compiles and keeps the manual track.
  doc.shots.clear();
  AuthoringShot solo;
  solo.id = "s0";
  solo.duration_frames = 30;
  AuthoringNode solo_node;
  solo_node.id = "headline";
  solo_node.type = "text";
  solo_node.text = "TECH";
  AuthoringAnimation solo_anim;
  solo_anim.property = "opacity";
  solo_anim.keyframes = {AuthoringKeyframe{.frame = 0, .value = 0.5},
                         AuthoringKeyframe{.frame = 15, .value = 1.0}};
  solo_node.animations.push_back(std::move(solo_anim));
  solo.nodes.push_back(std::move(solo_node));
  doc.shots.push_back(std::move(solo));

  auto ok = instantee::compiler::compile_authoring(doc, options);
  expect(ok.has_value(), "author-only compiles");
  if (ok) {
    expect(ok->animation_tracks.size() == 1, "one author track");
    expect(ok->animation_tracks.front().target_id == "headline", "target");
    expect(ok->animation_tracks.front().keyframes.front().value == 0.5,
           "author values preserved");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
