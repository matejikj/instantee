#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_metal/device.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_metal {

// Manifest entry for an offline-compiled metallib module. Bytes are typically
// embedded at build time; the registry never branches on style package names.
struct ShaderModuleDesc {
  std::string id;
  std::uint32_t version = 1;
  std::string feature_id;
  std::vector<std::string> entry_points{};
  std::span<const std::uint8_t> metallib_bytes{};
};

struct ShaderModuleValidation {
  std::string id;
  std::uint32_t version = 1;
  std::size_t metallib_bytes = 0;
  std::vector<std::string> resolved_entry_points{};
};

// Loads the metallib on the given device and verifies every declared entry
// point resolves. Used by smoke/validation tests for offline shader modules.
[[nodiscard]] std::expected<ShaderModuleValidation, diagnostics::Error>
validate_shader_module(Device &device, const ShaderModuleDesc &module);

// Built-in offline modules compiled from engine/shaders into metallib.
[[nodiscard]] ShaderModuleDesc test_pattern_shader_module();
[[nodiscard]] ShaderModuleDesc shared_surface_shader_module();
[[nodiscard]] ShaderModuleDesc halftone_shader_module();
[[nodiscard]] ShaderModuleDesc print_grain_shader_module();
[[nodiscard]] ShaderModuleDesc ink_stroke_shader_module();

[[nodiscard]] std::vector<ShaderModuleDesc> builtin_shader_modules();

} // namespace instantee::render_metal
