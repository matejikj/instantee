#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/renderer.hpp>
#include <instantee/render_metal/device.hpp>

#include <cstdint>
#include <expected>
#include <memory>

namespace instantee::render_metal {

// Single Metal submit owner implementing the render-core Renderer port.
// Serializes submit calls for one Device; does not introduce a second lease
// model (FrameLease stays in runtime). Metal/Apple types stay in the .cpp.
class MetalSubmitOwner final : public render_core::Renderer {
public:
  MetalSubmitOwner(MetalSubmitOwner &&) noexcept;
  MetalSubmitOwner &operator=(MetalSubmitOwner &&) noexcept;
  ~MetalSubmitOwner() override;

  MetalSubmitOwner(const MetalSubmitOwner &) = delete;
  MetalSubmitOwner &operator=(const MetalSubmitOwner &) = delete;

  // Takes ownership of an opened Device. The Device must outlive in-flight
  // GPU work started through this owner (none is retained across submit yet
  // beyond an immediate command-buffer commit handshake).
  [[nodiscard]] static std::expected<MetalSubmitOwner, diagnostics::Error>
  create(Device device);

  [[nodiscard]] const Device &device() const noexcept;
  [[nodiscard]] Device &device() noexcept;

  // Validates PassDesc, rejects lost devices / concurrent submit, then drains
  // an autorelease pool around a Metal command-buffer handshake. Real draw
  // encoding arrives with later resource-registry work; this establishes the
  // production submit ownership boundary.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  submit(const render_core::PassDesc &pass) override;

  [[nodiscard]] std::uint64_t submit_count() const noexcept;

private:
  struct Impl;
  explicit MetalSubmitOwner(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace instantee::render_metal
