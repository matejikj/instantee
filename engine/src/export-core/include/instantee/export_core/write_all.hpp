#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>

namespace instantee::export_core {

// Low-level write callback used by write_all. Tests inject short writes, EINTR,
// ENOSPC and EIO without touching production paths. Returns bytes written, or
// -1 and sets errno like ::write.
using WriteFn =
    std::function<std::ptrdiff_t(int fd, const void *data, std::size_t size)>;

// Writes the entire buffer. Retries EINTR. Treats a zero-length successful
// write before completion as an I/O error. Never publishes; callers own any
// rename.
[[nodiscard]] std::expected<void, diagnostics::Error>
write_all(int fd, std::span<const std::byte> data, WriteFn write_fn = {});

} // namespace instantee::export_core
