#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/render_target_pool.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/render_job.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

namespace {

using instantee::diagnostics::ErrorKind;
using instantee::diagnostics::ExitCode;
using instantee::render_core::GpuResourceDesc;
using instantee::render_core::RenderTargetPool;
using instantee::render_core::RenderTargetPoolConfig;
using instantee::runtime::CompiledSceneDraft;
using instantee::runtime::estimate_rgba8_bytes;
using instantee::runtime::make_compiled_scene;
using instantee::runtime::RenderJob;
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
  draft.canvas_width = 4;
  draft.canvas_height = 4;
  draft.duration_frames = instantee::core::FrameIndex{8};
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

void check_estimate_and_preflight() {
  auto ok = estimate_rgba8_bytes(4, 4);
  expect(ok.has_value() && *ok == 64U, "4x4 rgba bytes");
  auto overflow =
      estimate_rgba8_bytes(std::numeric_limits<std::uint32_t>::max(),
                           std::numeric_limits<std::uint32_t>::max());
  expect(!overflow.has_value(), "overflow rejected");
  expect(overflow.error().kind == ErrorKind::resource_budget_exceeded,
         "overflow kind");

  ResourceBudget budget;
  budget.max_accounted_bytes = 100;
  budget.max_in_flight_frames = 2;
  RenderJob job(sample_scene(), budget);

  auto frame = estimate_rgba8_bytes(4, 4);
  expect(frame.has_value(), "frame estimate");
  expect(!job.would_exceed_accounted(*frame), "preflight fits");
  expect(job.acquire_frame_slot(*frame).has_value(), "acquire after estimate");
  expect(job.remaining_accounted_bytes() == 100U - *frame, "remaining");
  expect(job.would_exceed_accounted(*frame), "second frame would exceed");
  expect(!job.acquire_frame_slot(*frame).has_value(), "controlled reject");
  expect(job.first_error()->kind == ErrorKind::resource_budget_exceeded,
         "budget error latched");
  // No silent downgrade: request size unchanged conceptually (still 4x4).
  expect(job.budget().max_accounted_bytes == 100U, "ceiling unchanged");
}

void check_pool_pressure_no_downgrade() {
  RenderTargetPoolConfig cfg{};
  cfg.registry_capacity = 4;
  cfg.max_pooled_bytes = 64;
  cfg.max_allocation_bytes = 64;
  auto pool = RenderTargetPool::create(cfg);
  expect(pool.has_value(), "pool");

  const GpuResourceDesc a{.width_px = 8, .height_px = 2};
  const GpuResourceDesc b{.width_px = 4, .height_px = 4};
  {
    auto lease = pool->acquire(a);
    expect(lease.has_value(), "first");
  }
  auto reused_room = pool->acquire(b);
  expect(reused_room.has_value(), "eviction enabled second size");
  expect(reused_room->desc().width_px == 4 &&
             reused_room->desc().height_px == 4,
         "no downscale");

  // Live pressure: hold the only budgeted target; further acquire must reject.
  auto blocked = pool->acquire(a);
  expect(!blocked.has_value(), "live budget reject");
  expect(blocked.error().code == "resource_budget_exceeded", "reject code");
  expect(blocked.error().message.find("quality is not reduced") !=
             std::string::npos,
         "no quality downgrade");
}

void check_composed_job_and_pool() {
  // Job memory ceiling matches one RGBA frame; pool matches the same ceiling.
  auto bytes = estimate_rgba8_bytes(4, 4);
  expect(bytes.has_value(), "bytes");

  ResourceBudget budget;
  budget.max_accounted_bytes = *bytes;
  budget.max_in_flight_frames = 1;
  budget.max_output_frames = 2;
  RenderJob job(sample_scene(), budget);

  RenderTargetPoolConfig cfg{};
  cfg.registry_capacity = 2;
  cfg.max_pooled_bytes = *bytes;
  cfg.max_allocation_bytes = *bytes;
  auto pool = RenderTargetPool::create(cfg);
  expect(pool.has_value(), "composed pool");

  expect(job.acquire_frame_slot(*bytes).has_value(), "job slot");
  auto lease = pool->acquire(GpuResourceDesc{.width_px = 4, .height_px = 4});
  expect(lease.has_value(), "pool lease under joint budget");

  // Second concurrent frame: both job and pool reject rather than
  // OOM/downgrade.
  expect(job.would_exceed_accounted(*bytes), "job preflight deny");
  expect(!job.acquire_frame_slot(*bytes).has_value(), "job reject");
  expect(!pool->acquire(GpuResourceDesc{.width_px = 4, .height_px = 4})
              .has_value(),
         "pool reject while live");

  lease->release();
  job.release_frame_slot(*bytes);
  expect(job.note_frame_appended().has_value(), "frame 1");
  expect(job.note_frame_appended().has_value(), "frame 2");
  expect(!job.note_frame_appended().has_value(), "output frame budget");
  expect(job.first_error()->exit_code == ExitCode::resource_budget_exceeded ||
             job.first_error()->kind == ErrorKind::resource_budget_exceeded,
         "sticky budget kind");
}

} // namespace

int main() {
  check_estimate_and_preflight();
  check_pool_pressure_no_downgrade();
  check_composed_job_and_pool();

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
