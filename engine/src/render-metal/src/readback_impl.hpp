#pragma once

#include <instantee/render_metal/readback.hpp>

#include "metal_owner.hpp"

#include <Metal/Metal.hpp>

#include <cstddef>
#include <cstdint>

namespace instantee::render_metal {

struct ReadbackLease::Impl {
  detail::MetalOwner<MTL::CommandBuffer> command;
  detail::MetalOwner<MTL::Buffer> staging;
  detail::MetalOwner<MTL::Texture> target;
  std::size_t bytes_per_row = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  State state = State::in_flight;

  void wait_for_gpu() noexcept {
    if (!command || state != State::in_flight) {
      return;
    }
    command->waitUntilCompleted();
    if (command->status() == MTL::CommandBufferStatusError) {
      state = State::failed;
      return;
    }
    state = State::ready;
  }

  void retire_gpu() noexcept {
    if (!command) {
      return;
    }
    const auto status = command->status();
    if (status != MTL::CommandBufferStatusCompleted &&
        status != MTL::CommandBufferStatusError) {
      command->waitUntilCompleted();
    }
    if (state == State::in_flight) {
      if (command->status() == MTL::CommandBufferStatusError) {
        state = State::failed;
      } else {
        state = State::ready;
      }
    }
  }
};

} // namespace instantee::render_metal
