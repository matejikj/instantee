#pragma once

#include <instantee/export_apple/pixel_buffer.hpp>

#include <CoreVideo/CoreVideo.h>

namespace instantee::spike::apple::detail {

// Borrows the Core Video buffer owned by the C++ wrapper. The PixelBuffer must
// outlive the use of the returned reference.
[[nodiscard]] CVPixelBufferRef
borrow_cv_pixel_buffer(PixelBuffer &buffer) noexcept;

[[nodiscard]] bool pixel_buffer_empty(const PixelBuffer &buffer) noexcept;

} // namespace instantee::spike::apple::detail
