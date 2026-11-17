#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>

#include <expected>
#include <optional>

namespace instantee::render_core {

// Platform-neutral renderer port. Implementations live behind PImpl in
// backend modules (e.g. render-metal). No FrameLease here: admission stays in
// runtime; this port only describes and submits draws.
class Renderer {
public:
  virtual ~Renderer() noexcept = default;

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) noexcept = default;
  Renderer &operator=(Renderer &&) noexcept = default;

  // Rejects unbound targets, empty extents, and structurally invalid ops.
  // Does not allocate GPU resources; backends may still fail with gpu_failure.
  [[nodiscard]] virtual std::expected<void, diagnostics::Error>
  submit(const PassDesc &pass) = 0;

protected:
  Renderer() = default;
};

// Validates PassDesc before any backend touch. Shared by fake and real devices.
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_pass(const PassDesc &pass);

// CPU stand-in used by unit tests and dependency checks. Records the last
// accepted pass; never links Apple/Metal frameworks.
class FakeRenderer final : public Renderer {
public:
  FakeRenderer() = default;

  [[nodiscard]] std::expected<void, diagnostics::Error>
  submit(const PassDesc &pass) override;

  [[nodiscard]] bool has_submitted() const noexcept { return submitted_; }
  [[nodiscard]] const PassDesc &last_pass() const noexcept { return last_; }
  [[nodiscard]] std::uint64_t submit_count() const noexcept {
    return submit_count_;
  }

  // Next submit fails with the provided error after validation succeeds.
  void fail_next(diagnostics::Error error) { fail_next_ = std::move(error); }

private:
  bool submitted_ = false;
  std::uint64_t submit_count_ = 0;
  PassDesc last_{};
  std::optional<diagnostics::Error> fail_next_{};
};

} // namespace instantee::render_core
