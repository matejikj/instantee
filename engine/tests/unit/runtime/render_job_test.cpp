#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/render_job.hpp>

#include <cstdlib>
#include <iostream>
#include <stop_token>

namespace {

using instantee::diagnostics::ErrorKind;
using instantee::runtime::CompiledSceneDraft;
using instantee::runtime::is_published_phase;
using instantee::runtime::is_terminal_phase;
using instantee::runtime::make_compiled_scene;
using instantee::runtime::render_job_phase_id;
using instantee::runtime::RenderJob;
using instantee::runtime::RenderJobPhase;
using instantee::runtime::ResourceBudget;
using instantee::runtime::RuntimeNode;
using instantee::runtime::RuntimeNodeKind;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::unique_ptr<const instantee::runtime::CompiledScene>
sample_scene() {
  CompiledSceneDraft draft;
  draft.canvas_width = 16;
  draft.canvas_height = 16;
  draft.duration_frames = instantee::core::FrameIndex{2};
  draft.seed = 1;
  draft.node_ids = {"n0"};
  RuntimeNode node;
  node.kind = RuntimeNodeKind::geometry;
  draft.nodes = {node};
  auto scene = make_compiled_scene(std::move(draft));
  if (!scene) {
    return nullptr;
  }
  return std::move(*scene);
}

} // namespace

int main() {
  {
    expect(render_job_phase_id(RenderJobPhase::publishing) == "publishing",
           "phase id");
    expect(!is_terminal_phase(RenderJobPhase::publishing),
           "publishing not terminal");
    expect(is_published_phase(RenderJobPhase::published_durability_unknown),
           "published* flag");
  }

  {
    auto scene = sample_scene();
    expect(scene != nullptr, "sample scene");
    if (!scene) {
      return EXIT_FAILURE;
    }
    RenderJob job(std::move(scene));
    expect(job.phase() == RenderJobPhase::created, "starts created");
    expect(job.enter(RenderJobPhase::preparing).has_value(), "→ preparing");
    expect(job.enter(RenderJobPhase::rendering).has_value(), "→ rendering");
    expect(job.enter(RenderJobPhase::finishing).has_value(), "→ finishing");
    expect(job.enter(RenderJobPhase::publishing).has_value(), "→ publishing");
    expect(job.note_publish_outcome(true, true, false).has_value(),
           "publish ok");
    expect(job.phase() == RenderJobPhase::completed, "completed");
    expect(is_terminal_phase(job.phase()), "completed terminal");
  }

  {
    auto scene = sample_scene();
    RenderJob job(std::move(scene));
    expect(job.enter(RenderJobPhase::preparing).has_value(), "prep");
    expect(job.enter(RenderJobPhase::rendering).has_value(), "render");
    expect(job.cancel().has_value(), "cancel before publish");
    expect(job.phase() == RenderJobPhase::cancelled, "cancelled");
    expect(!job.enter(RenderJobPhase::finishing).has_value(),
           "no advance after cancel");
  }

  {
    auto scene = sample_scene();
    RenderJob job(std::move(scene));
    expect(job.enter(RenderJobPhase::preparing).has_value(), "prep");
    expect(job.enter(RenderJobPhase::rendering).has_value(), "render");
    expect(job.enter(RenderJobPhase::finishing).has_value(), "finish");
    std::stop_source source;
    source.request_stop();
    auto entered = job.enter(RenderJobPhase::publishing, source.get_token());
    expect(!entered.has_value(), "stop wins before publish");
    expect(entered.error().kind == ErrorKind::cancelled, "cancel kind");
    expect(job.phase() == RenderJobPhase::cancelled, "cancelled phase");
  }

  {
    auto scene = sample_scene();
    RenderJob job(std::move(scene));
    expect(job.enter(RenderJobPhase::preparing).has_value(), "prep");
    expect(job.enter(RenderJobPhase::rendering).has_value(), "render");
    expect(job.enter(RenderJobPhase::finishing).has_value(), "finish");
    expect(job.enter(RenderJobPhase::publishing).has_value(), "publishing");
    expect(!job.cancel().has_value(), "cancel blocked after publish gate");
    expect(job.note_publish_outcome(true, false, false).has_value(),
           "durability unknown");
    expect(job.phase() == RenderJobPhase::published_durability_unknown,
           "published_durability_unknown");
  }

  {
    auto scene = sample_scene();
    RenderJob job(std::move(scene));
    expect(job.enter(RenderJobPhase::preparing).has_value(), "prep");
    expect(job.enter(RenderJobPhase::rendering).has_value(), "render");
    expect(job.enter(RenderJobPhase::finishing).has_value(), "finish");
    expect(job.enter(RenderJobPhase::publishing).has_value(), "publishing");
    expect(job.note_publish_outcome(true, true, true).has_value(),
           "report failed");
    expect(job.phase() == RenderJobPhase::published_report_failed,
           "published_report_failed");
  }

  {
    ResourceBudget budget;
    budget.max_in_flight_frames = 1;
    budget.max_accounted_bytes = 100;
    budget.max_output_frames = 2;
    auto scene = sample_scene();
    RenderJob job(std::move(scene), budget);
    expect(job.acquire_frame_slot(40).has_value(), "slot 1");
    expect(!job.acquire_frame_slot(40).has_value(), "in-flight exhausted");
    job.release_frame_slot(40);
    expect(job.acquire_frame_slot(40).has_value(), "slot after release");
    job.release_frame_slot(40);
    expect(!job.acquire_frame_slot(101).has_value(), "memory budget");
    expect(job.note_frame_appended().has_value(), "frame 1");
    expect(job.note_frame_appended().has_value(), "frame 2");
    expect(!job.note_frame_appended().has_value(), "frame budget");
    expect(job.note_frame_appended().error().kind ==
               ErrorKind::resource_budget_exceeded,
           "budget kind");
    expect(job.first_error().has_value(), "budget latches first error");
  }

  {
    auto scene = sample_scene();
    RenderJob job(std::move(scene));
    expect(job.enter(RenderJobPhase::preparing).has_value(), "prep");
    expect(job.enter(RenderJobPhase::rendering).has_value(), "render");
    expect(job.enter(RenderJobPhase::finishing).has_value(), "finish");
    expect(job.enter(RenderJobPhase::publishing).has_value(), "publishing");
    expect(job.enter(RenderJobPhase::published).has_value(), "published");
    expect(job.enter(RenderJobPhase::published_durability_unknown).has_value(),
           "published → durability unknown");
    expect(job.phase() == RenderJobPhase::published_durability_unknown,
           "durability unknown terminal");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
