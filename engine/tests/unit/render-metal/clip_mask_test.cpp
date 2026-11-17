#include <instantee/render_core/clip_compositor.hpp>
#include <instantee/render_metal/clip_mask_renderer.hpp>
#include <instantee/render_metal/device.hpp>

#include <cstdlib>
#include <iostream>

namespace {

using namespace instantee::render_core;
using namespace instantee::render_metal;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool frames_match(const Rgba8Image &cpu, const Rgba8Frame &gpu,
                                std::uint8_t tol) {
  if (cpu.width_px != gpu.extent.width || cpu.height_px != gpu.extent.height ||
      cpu.rgba.size() != gpu.rgba.size()) {
    return false;
  }
  for (std::size_t i = 0; i < cpu.rgba.size(); ++i) {
    const int d = static_cast<int>(cpu.rgba[i]) - static_cast<int>(gpu.rgba[i]);
    if (d > tol || d < -static_cast<int>(tol)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] ClipScript nested_scissor_script() {
  ClipScript script;
  script.width_px = 32;
  script.height_px = 32;
  script.commands.push_back(
      ClipCommand{.kind = ClipCommandKind::draw,
                  .op = {.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::push_scissor,
                                        .scissor = {4, 4, 24, 24}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::push_scissor,
                                        .scissor = {8, 8, 8, 8}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::draw,
                                        .op = {.kind = DrawOpKind::fill_rect,
                                               .rect = {0, 0, 32, 32},
                                               .color = {255, 0, 0, 255}}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
  return script;
}

[[nodiscard]] ClipScript nested_mask_script() {
  ClipScript script;
  script.width_px = 32;
  script.height_px = 32;
  script.commands.push_back(
      ClipCommand{.kind = ClipCommandKind::draw,
                  .op = {.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}}});
  script.commands.push_back(
      ClipCommand{.kind = ClipCommandKind::push_shape_mask,
                  .op = {.kind = DrawOpKind::fill_ellipse,
                         .rect = {4, 4, 24, 24},
                         .color = {255, 255, 255, 255}}});
  script.commands.push_back(
      ClipCommand{.kind = ClipCommandKind::push_shape_mask,
                  .op = {.kind = DrawOpKind::fill_rect,
                         .rect = {10, 10, 12, 12},
                         .color = {255, 255, 255, 255}}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::draw,
                                        .op = {.kind = DrawOpKind::fill_rect,
                                               .rect = {0, 0, 32, 32},
                                               .color = {0, 255, 0, 255}}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
  return script;
}

[[nodiscard]] ClipScript layered_script() {
  ClipScript script;
  script.width_px = 32;
  script.height_px = 32;
  script.commands.push_back(ClipCommand{
      .kind = ClipCommandKind::draw,
      .op = {.kind = DrawOpKind::clear, .color = {20, 20, 20, 255}}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::push_scissor,
                                        .scissor = {0, 0, 16, 32}});
  script.commands.push_back(
      ClipCommand{.kind = ClipCommandKind::push_shape_mask,
                  .op = {.kind = DrawOpKind::fill_ellipse,
                         .rect = {2, 8, 20, 16},
                         .color = {255, 255, 255, 255}}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::draw,
                                        .op = {.kind = DrawOpKind::fill_rect,
                                               .rect = {0, 0, 32, 32},
                                               .color = {0, 0, 255, 255}}});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
  script.commands.push_back(ClipCommand{.kind = ClipCommandKind::pop});
  return script;
}

} // namespace

int main() {
  auto device = Device::open();
  expect(device.has_value(), "open device");
  if (!device) {
    return EXIT_FAILURE;
  }
  auto renderer = ClipMaskRenderer::create(*device);
  expect(renderer.has_value(), "create clip mask renderer");
  if (!renderer) {
    std::cerr << renderer.error().message << '\n';
    return EXIT_FAILURE;
  }

  const ClipScript scripts[] = {nested_scissor_script(), nested_mask_script(),
                                layered_script()};
  const char *names[] = {"nested_scissor", "nested_mask", "scissor_plus_mask"};
  for (std::size_t i = 0; i < 3; ++i) {
    auto cpu = rasterize_clip_script(scripts[i]);
    expect(cpu.has_value(), names[i]);
    auto gpu = renderer->render(scripts[i]);
    expect(gpu.has_value(), names[i]);
    if (cpu && gpu) {
      // Binary coverage goldens: allow tiny channel noise from GPU blend.
      expect(frames_match(*cpu, *gpu, /*tol=*/2), names[i]);
    } else if (!gpu) {
      std::cerr << names[i] << ": " << gpu.error().message << '\n';
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "clip_mask_test ok\n";
  return EXIT_SUCCESS;
}
