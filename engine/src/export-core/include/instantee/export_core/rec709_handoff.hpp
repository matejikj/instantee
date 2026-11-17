#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/export_core/frame_bridge.hpp>
#include <instantee/export_core/video_sink.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

namespace instantee::export_core {

// Packages display-referred Rec.709/sRGB-transfer RGBA8 (straight alpha) for
// the VideoSink/FrameBridge path. Semi-transparent pixels are flattened over
// opaque black so the encoder receives opaque RGB with a=255.
[[nodiscard]] std::expected<EncodableFrame, diagnostics::Error>
make_rec709_encodable_frame(std::uint32_t width_px, std::uint32_t height_px,
                            std::span<const std::uint8_t> straight_rec709_rgba8,
                            core::FrameIndex frame_index);

// Flatten + publish convenience for render backends that already produced
// Rec.709-encoded straight RGBA8. On success `frame` ownership moves into the
// bridge; on failure the caller's bytes are unchanged (no partial publish).
[[nodiscard]] std::expected<void, diagnostics::Error> publish_rec709_rgba8(
    FrameBridge &bridge, std::uint32_t width_px, std::uint32_t height_px,
    std::span<const std::uint8_t> straight_rec709_rgba8,
    core::FrameIndex frame_index, core::RationalTime pts, std::stop_token stop);

} // namespace instantee::export_core
