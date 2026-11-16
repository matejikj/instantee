#include <instantee/render_metal/readback.hpp>

#include "readback_impl.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace instantee::spike::metal {

ReadbackLease::ReadbackLease(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

ReadbackLease::ReadbackLease(ReadbackLease &&) noexcept = default;
ReadbackLease &ReadbackLease::operator=(ReadbackLease &&) noexcept = default;

ReadbackLease::~ReadbackLease() {
  if (!impl_) {
    return;
  }
  // Lease destruction is not a recycle point: finish GPU work first.
  impl_->retire_gpu();
}

ReadbackLease::State ReadbackLease::state() const noexcept {
  return impl_ ? impl_->state : State::consumed;
}

void ReadbackLease::cancel() noexcept {
  if (!impl_) {
    return;
  }
  if (impl_->state == State::in_flight || impl_->state == State::ready) {
    impl_->state = State::cancelled;
  }
}

std::expected<Rgba8Frame, Error> ReadbackLease::take_pixels() {
  if (!impl_ || !impl_->staging || !impl_->command) {
    return std::unexpected(Error{ExitCode::gpu_failure, "readback_not_ready",
                                 "metal", "Readback lease is not ready"});
  }
  if (impl_->state == State::cancelled) {
    return std::unexpected(Error{ExitCode::invalid_input, "cancelled", "metal",
                                 "Readback was cancelled"});
  }
  if (impl_->state == State::consumed) {
    return std::unexpected(Error{ExitCode::invalid_input, "already_consumed",
                                 "metal",
                                 "Readback pixels were already taken"});
  }
  if (impl_->state == State::failed) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "gpu_command_failed", "metal",
              "Metal command buffer completed with an error"});
  }

  if (impl_->state == State::in_flight) {
    impl_->wait_for_gpu();
  }
  if (impl_->state == State::failed) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "gpu_command_failed", "metal",
              "Metal command buffer completed with an error"});
  }
  if (impl_->state == State::cancelled) {
    return std::unexpected(Error{ExitCode::invalid_input, "cancelled", "metal",
                                 "Readback was cancelled"});
  }

  const auto tight_row =
      static_cast<std::size_t>(impl_->width) * kTestPatternChannels;
  const auto pixel_bytes = tight_row * static_cast<std::size_t>(impl_->height);
  if (pixel_bytes >
      static_cast<std::size_t>(
          std::numeric_limits<std::vector<std::uint8_t>::size_type>::max())) {
    return std::unexpected(Error{ExitCode::gpu_failure, "frame_too_large",
                                 "metal", "Frame byte size overflows"});
  }

  Rgba8Frame frame;
  frame.extent = Extent2u{impl_->width, impl_->height};
  try {
    frame.rgba.resize(pixel_bytes);
  } catch (const std::bad_alloc &) {
    return std::unexpected(Error{ExitCode::gpu_failure, "out_of_memory",
                                 "metal",
                                 "Cannot allocate frame pixel storage"});
  }

  const auto *source =
      static_cast<const std::uint8_t *>(impl_->staging->contents());
  if (source == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "staging_map_failed",
                                 "metal",
                                 "Staging buffer contents are unavailable"});
  }
  for (std::uint32_t y = 0; y < impl_->height; ++y) {
    std::memcpy(frame.rgba.data() + static_cast<std::size_t>(y) * tight_row,
                source + static_cast<std::size_t>(y) * impl_->bytes_per_row,
                tight_row);
  }

  impl_->state = State::consumed;
  // Staging is no longer required after a successful host copy.
  impl_->staging.reset();
  impl_->target.reset();
  impl_->command.reset();
  return frame;
}

} // namespace instantee::spike::metal
