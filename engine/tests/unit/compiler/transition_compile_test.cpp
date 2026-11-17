#include <instantee/compiler/compile.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/shot_timeline.hpp>
#include <instantee/runtime/transition_schedule.hpp>
#include <instantee/scene_ir/authoring.hpp>

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using instantee::core::FrameIndex;
using instantee::runtime::make_shot_timeline;
using instantee::runtime::make_transition_schedule;
using instantee::runtime::RuntimeShot;
using instantee::runtime::TransitionBlend;
using instantee::runtime::TransitionSpanDesc;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] instantee::scene_ir::AuthoringDocument
make_two_shot_doc(std::uint64_t a_frames, std::uint64_t b_frames) {
  instantee::scene_ir::AuthoringDocument doc;
  doc.schema_version = "1.0.0";
  doc.film_id = "transition-film";
  doc.canvas_width = 1080;
  doc.canvas_height = 1920;
  doc.frame_rate = instantee::core::FrameRate::media_30_1();
  doc.seed = 7;
  doc.style = "editorial@1";

  instantee::scene_ir::AuthoringShot a;
  a.id = "intro";
  a.duration_frames = a_frames;
  instantee::scene_ir::AuthoringNode n0;
  n0.id = "headline";
  n0.type = "text";
  n0.text = "A";
  a.nodes.push_back(n0);

  instantee::scene_ir::AuthoringShot b;
  b.id = "outro";
  b.duration_frames = b_frames;
  instantee::scene_ir::AuthoringNode n1;
  n1.id = "endcard";
  n1.type = "text";
  n1.text = "B";
  b.nodes.push_back(n1);

  doc.shots.push_back(std::move(a));
  doc.shots.push_back(std::move(b));
  return doc;
}

[[nodiscard]] std::int32_t
max_layer_for_shot(const instantee::scene_ir::SceneDocument &doc,
                   const instantee::scene_ir::Shot &shot) {
  std::unordered_map<std::string, std::int32_t> layers;
  for (const auto &node : doc.nodes) {
    layers.emplace(node.id, node.layer_order);
  }
  std::int32_t max_layer = 0;
  bool any = false;
  for (const auto &id : shot.node_ids) {
    if (id == "main_cam") {
      continue;
    }
    const auto it = layers.find(id);
    if (it == layers.end()) {
      continue;
    }
    if (!any || it->second > max_layer) {
      max_layer = it->second;
      any = true;
    }
  }
  return max_layer;
}

} // namespace

int main() {
  instantee::compiler::CompileOptions options;
  const std::uint64_t packed = 100 + 40;

  // Hard cut compile: duration unchanged, span at boundary.
  {
    auto doc = make_two_shot_doc(100, 40);
    instantee::scene_ir::AuthoringTransition cut;
    cut.id = "cut1";
    cut.from_shot_id = "intro";
    cut.to_shot_id = "outro";
    cut.overlap_frames = 0;
    cut.blend = "cut";
    doc.transitions.push_back(cut);

    auto compiled = instantee::compiler::compile_authoring(doc, options);
    expect(compiled.has_value(), "compile hard cut");
    if (compiled) {
      expect(compiled->duration_frames == FrameIndex{packed},
             "cut film duration");
      expect(compiled->transition_spans.size() == 1, "one cut span");
      if (!compiled->transition_spans.empty()) {
        expect(compiled->transition_spans[0].blend == "cut" &&
                   compiled->transition_spans[0].duration_frames == 0 &&
                   compiled->transition_spans[0].start_frame == 100,
               "cut span fields");
      }
      expect(max_layer_for_shot(*compiled, compiled->shots[1]) >
                 max_layer_for_shot(*compiled, compiled->shots[0]),
             "incoming shot layers above outgoing");
    }
  }

  // Dissolve (reveal) compile: no double-count; runtime schedule accepts.
  {
    auto doc = make_two_shot_doc(100, 40);
    instantee::scene_ir::AuthoringTransition dissolve;
    dissolve.id = "dissolve1";
    dissolve.from_shot_id = "intro";
    dissolve.to_shot_id = "outro";
    dissolve.overlap_frames = 20;
    dissolve.blend = "dissolve";
    doc.transitions.push_back(dissolve);

    auto compiled = instantee::compiler::compile_authoring(doc, options);
    expect(compiled.has_value(), "compile dissolve");
    if (compiled) {
      expect(compiled->duration_frames == FrameIndex{packed},
             "dissolve does not extend duration");
      expect(compiled->transition_spans.size() == 1 &&
                 compiled->transition_spans[0].blend == "dissolve" &&
                 compiled->transition_spans[0].start_frame == 90 &&
                 compiled->transition_spans[0].duration_frames == 20,
             "dissolve span straddles boundary");
      expect(max_layer_for_shot(*compiled, compiled->shots[1]) >
                 max_layer_for_shot(*compiled, compiled->shots[0]),
             "dissolve layer order");

      std::vector<RuntimeShot> runtime(2);
      runtime[0].id = compiled->shots[0].id;
      runtime[0].start_frame = compiled->shots[0].start_frame;
      runtime[0].duration_frames = compiled->shots[0].duration_frames;
      runtime[1].id = compiled->shots[1].id;
      runtime[1].start_frame = compiled->shots[1].start_frame;
      runtime[1].duration_frames = compiled->shots[1].duration_frames;
      auto timeline = make_shot_timeline(runtime, compiled->duration_frames);
      expect(timeline.has_value(), "runtime timeline");
      if (timeline) {
        TransitionSpanDesc desc;
        desc.id = compiled->transition_spans[0].id;
        desc.from_shot_index = 0;
        desc.to_shot_index = 1;
        desc.start = FrameIndex{compiled->transition_spans[0].start_frame};
        desc.duration_frames = compiled->transition_spans[0].duration_frames;
        desc.blend = TransitionBlend::dissolve;
        auto schedule =
            make_transition_schedule(*timeline, std::span{&desc, 1});
        expect(schedule.has_value(), "runtime schedule from compiled span");
        if (schedule) {
          expect(schedule->output_duration() == compiled->duration_frames,
                 "compiled transition has no double-counted frames");
        }
      }

      auto json = instantee::compiler::scene_document_to_json(*compiled);
      expect(json.has_value() &&
                 json->find("\"transition_spans\":[{") != std::string::npos &&
                 json->find("dissolve1") != std::string::npos,
             "IR JSON emits transition spans");
    }
  }

  // Wipe compile path.
  {
    auto doc = make_two_shot_doc(80, 80);
    instantee::scene_ir::AuthoringTransition wipe;
    wipe.id = "wipe1";
    wipe.from_shot_id = "intro";
    wipe.to_shot_id = "outro";
    wipe.overlap_frames = 16;
    wipe.blend = "wipe";
    doc.transitions.push_back(wipe);
    auto compiled = instantee::compiler::compile_authoring(doc, options);
    expect(compiled.has_value() &&
               compiled->duration_frames == FrameIndex{160} &&
               compiled->transition_spans.size() == 1 &&
               compiled->transition_spans[0].blend == "wipe",
           "wipe compile");
  }

  // Fail closed: dissolve with zero overlap.
  {
    auto doc = make_two_shot_doc(50, 50);
    instantee::scene_ir::AuthoringTransition bad;
    bad.id = "bad";
    bad.from_shot_id = "intro";
    bad.to_shot_id = "outro";
    bad.overlap_frames = 0;
    bad.blend = "dissolve";
    doc.transitions.push_back(bad);
    expect(!instantee::compiler::compile_authoring(doc, options).has_value(),
           "reject zero-overlap dissolve");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
