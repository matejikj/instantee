#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/renderer.hpp>

#include <cstdlib>
#include <iostream>

// Compile-time guard: this production TU must never see Metal/Apple macros
// introduced by platform headers. Absence of includes is the real barrier;
// these checks catch accidental transitive pollution.
#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

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
  using namespace instantee::render_core;
  using namespace instantee::diagnostics;

  FakeRenderer renderer;

  PassDesc bad_extent{};
  bad_extent.width_px = 0;
  bad_extent.height_px = 1080;
  bad_extent.color_target = 1;
  bad_extent.ops.push_back(DrawOp{.kind = DrawOpKind::clear});
  expect(!renderer.submit(bad_extent).has_value(), "reject zero extent");

  PassDesc unbound{};
  unbound.width_px = 64;
  unbound.height_px = 64;
  unbound.color_target = 0;
  unbound.ops.push_back(DrawOp{.kind = DrawOpKind::clear});
  expect(!renderer.submit(unbound).has_value(), "reject unbound target");

  PassDesc empty{};
  empty.width_px = 64;
  empty.height_px = 64;
  empty.color_target = 7;
  expect(!renderer.submit(empty).has_value(), "reject empty ops");

  PassDesc bad_rect{};
  bad_rect.width_px = 64;
  bad_rect.height_px = 64;
  bad_rect.color_target = 7;
  bad_rect.ops.push_back(DrawOp{
      .kind = DrawOpKind::fill_rect,
      .rect = Rect2f{.x = 0, .y = 0, .width = 0, .height = 8},
      .color = Rgba8{.r = 255, .g = 0, .b = 0, .a = 255},
  });
  expect(!renderer.submit(bad_rect).has_value(), "reject invalid fill_rect");

  PassDesc ok{};
  ok.width_px = 64;
  ok.height_px = 64;
  ok.color_target = 7;
  ok.ops.push_back(DrawOp{.kind = DrawOpKind::clear,
                          .color = Rgba8{.r = 0, .g = 0, .b = 0, .a = 255}});
  ok.ops.push_back(DrawOp{
      .kind = DrawOpKind::fill_rect,
      .rect = Rect2f{.x = 4, .y = 4, .width = 16, .height = 16},
      .color = Rgba8{.r = 0, .g = 255, .b = 0, .a = 255},
  });
  expect(renderer.submit(ok).has_value(), "accept valid pass");
  expect(renderer.has_submitted(), "recorded submission");
  expect(renderer.submit_count() == 1, "one submit");
  expect(renderer.last_pass().ops.size() == 2, "ops preserved");

  renderer.fail_next(Error{ExitCode::gpu_failure, ErrorKind::gpu_failure,
                           "injected_gpu_failure", "render_core",
                           "forced failure"});
  expect(!renderer.submit(ok).has_value(), "injected backend failure");
  expect(renderer.submit_count() == 1, "failed submit does not count");

  expect(renderer.submit(ok).has_value(), "recover after injected failure");
  expect(renderer.submit_count() == 2, "second successful submit");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
