#include <instantee/export_core/write_all.hpp>

#include <cerrno>
#include <unistd.h>

namespace instantee::export_core {
namespace {

constexpr const char *kPhase = "write_all";

[[nodiscard]] diagnostics::Error io_error(const char *code,
                                          const char *message) {
  return diagnostics::Error{diagnostics::ExitCode::export_failure, code, kPhase,
                            message};
}

} // namespace

std::expected<void, diagnostics::Error>
write_all(int fd, std::span<const std::byte> data, WriteFn write_fn) {
  if (fd < 0) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::invalid_input, "invalid_fd", kPhase,
        "write_all requires a valid file descriptor"});
  }
  if (!write_fn) {
    write_fn = [](int write_fd, const void *buffer,
                  std::size_t size) -> std::ptrdiff_t {
      return ::write(write_fd, buffer, size);
    };
  }

  std::size_t offset = 0;
  while (offset < data.size()) {
    const std::size_t remaining = data.size() - offset;
    const std::ptrdiff_t wrote = write_fn(fd, data.data() + offset, remaining);
    if (wrote < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == ENOSPC) {
        return std::unexpected(
            io_error("enospc", "write_all failed with ENOSPC"));
      }
      if (errno == EIO) {
        return std::unexpected(io_error("eio", "write_all failed with EIO"));
      }
      return std::unexpected(
          io_error("write_failed", "write_all failed with I/O error"));
    }
    if (wrote == 0) {
      return std::unexpected(
          io_error("short_write", "write_all observed zero-byte progress"));
    }
    offset += static_cast<std::size_t>(wrote);
  }
  return {};
}

} // namespace instantee::export_core
