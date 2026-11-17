#include <instantee/export_apple/apple_video_sink.hpp>

#include <cstring>
#include <utility>
#include <vector>

namespace instantee::export_apple {
namespace {

using diagnostics::Error;
using diagnostics::ErrorKind;
using diagnostics::ExitCode;
using spike::apple::PixelBufferFormat;
using spike::apple::PixelBufferPool;
using spike::apple::VideoWriter;
using spike::apple::VideoWriterConfig;

[[nodiscard]] Error make_error(ExitCode exit_code, ErrorKind kind,
                               std::string_view code,
                               std::string_view message) {
  return Error{exit_code, kind, code, "apple_video_sink", message};
}

} // namespace

struct AppleVideoSink::State {
  std::filesystem::path output_path;
  std::uint32_t pool_capacity = 4;
  std::optional<VideoWriter> writer;
  std::optional<PixelBufferPool> pool;
  export_core::VideoSettings settings{};
  std::uint64_t frames_appended = 0;
  bool begun = false;
  bool finished = false;
  bool cancelled = false;
};

AppleVideoSink::AppleVideoSink(std::unique_ptr<State> state) noexcept
    : state_(std::move(state)) {}

AppleVideoSink::AppleVideoSink(AppleVideoSink &&) noexcept = default;
AppleVideoSink &AppleVideoSink::operator=(AppleVideoSink &&) noexcept = default;
AppleVideoSink::~AppleVideoSink() = default;

std::expected<std::unique_ptr<AppleVideoSink>, Error>
AppleVideoSink::create(std::filesystem::path output_path,
                       std::uint32_t pool_capacity) {
  if (output_path.empty()) {
    return std::unexpected(
        make_error(ExitCode::invalid_input, ErrorKind::invalid_input,
                   "empty_path", "AppleVideoSink requires an output path"));
  }
  if (pool_capacity == 0) {
    return std::unexpected(make_error(ExitCode::invalid_input,
                                      ErrorKind::invalid_input, "empty_pool",
                                      "pool_capacity must be > 0"));
  }
  auto state = std::make_unique<State>();
  state->output_path = std::move(output_path);
  state->pool_capacity = pool_capacity;
  return std::unique_ptr<AppleVideoSink>(new AppleVideoSink(std::move(state)));
}

const std::filesystem::path &AppleVideoSink::output_path() const noexcept {
  return state_->output_path;
}

std::expected<void, Error>
AppleVideoSink::begin(const export_core::VideoSettings &settings) {
  if (!state_ || state_->begun) {
    return std::unexpected(make_error(ExitCode::invalid_input,
                                      ErrorKind::invalid_input, "already_begun",
                                      "AppleVideoSink already begun"));
  }
  if (settings.width == 0 || settings.height == 0) {
    return std::unexpected(
        make_error(ExitCode::invalid_input, ErrorKind::invalid_input,
                   "invalid_settings", "width and height must be > 0"));
  }

  VideoWriterConfig config;
  config.output_path = state_->output_path;
  config.format = PixelBufferFormat{settings.width, settings.height};
  config.frame_rate = settings.frame_rate;

  auto writer = VideoWriter::create(std::move(config));
  if (!writer) {
    return std::unexpected(writer.error());
  }
  auto pool = PixelBufferPool::create(
      PixelBufferFormat{settings.width, settings.height},
      state_->pool_capacity);
  if (!pool) {
    return std::unexpected(pool.error());
  }

  state_->writer = std::move(*writer);
  state_->pool = std::move(*pool);
  state_->settings = settings;
  state_->begun = true;
  return {};
}

std::expected<void, Error>
AppleVideoSink::append(export_core::EncodableFrame &frame,
                       core::RationalTime /*pts*/, std::stop_token stop) {
  if (!state_ || !state_->begun || !state_->writer || !state_->pool) {
    return std::unexpected(make_error(ExitCode::invalid_input,
                                      ErrorKind::invalid_input, "not_begun",
                                      "AppleVideoSink requires begin()"));
  }
  if (state_->finished || state_->cancelled) {
    return std::unexpected(
        make_error(ExitCode::invalid_input, ErrorKind::invalid_input, "closed",
                   "AppleVideoSink is finished or cancelled"));
  }
  if (stop.stop_requested()) {
    cancel();
    return std::unexpected(make_error(ExitCode::cancelled, ErrorKind::cancelled,
                                      "cancelled",
                                      "stop requested during append"));
  }
  if (frame.width() != state_->settings.width ||
      frame.height() != state_->settings.height) {
    return std::unexpected(make_error(ExitCode::invalid_input,
                                      ErrorKind::invalid_input, "size_mismatch",
                                      "frame size must match VideoSettings"));
  }

  auto buffer = state_->pool->acquire(stop);
  if (!buffer) {
    return std::unexpected(buffer.error());
  }

  const auto *bytes = frame.data();
  const auto byte_count = frame.byte_size();
  std::vector<std::uint8_t> rgba(byte_count);
  if (bytes != nullptr && byte_count > 0) {
    std::memcpy(rgba.data(), bytes, byte_count);
  }
  if (auto filled = buffer->fill_rgba8(rgba); !filled) {
    return std::unexpected(filled.error());
  }

  if (auto appended =
          state_->writer->append(*buffer, frame.frame_index(), stop);
      !appended) {
    return std::unexpected(appended.error());
  }

  // Successful append consumes the caller's pixels per VideoSink contract.
  (void)frame.release_pixels();
  ++state_->frames_appended;
  return {};
}

std::expected<export_core::ExportSummary, Error> AppleVideoSink::finish() {
  if (!state_ || !state_->begun || !state_->writer) {
    return std::unexpected(make_error(ExitCode::invalid_input,
                                      ErrorKind::invalid_input, "not_begun",
                                      "AppleVideoSink requires begin()"));
  }
  if (state_->cancelled) {
    return export_core::ExportSummary{.frames_appended =
                                          state_->frames_appended,
                                      .finished = false,
                                      .cancelled = true};
  }
  if (state_->finished) {
    return export_core::ExportSummary{.frames_appended =
                                          state_->frames_appended,
                                      .finished = true,
                                      .cancelled = false};
  }
  if (auto done = state_->writer->finish(); !done) {
    return std::unexpected(done.error());
  }
  state_->finished = true;
  if (state_->pool) {
    state_->pool->close();
  }
  return export_core::ExportSummary{.frames_appended = state_->frames_appended,
                                    .finished = true,
                                    .cancelled = false};
}

void AppleVideoSink::cancel() noexcept {
  if (!state_) {
    return;
  }
  state_->cancelled = true;
  if (state_->writer) {
    state_->writer->cancel();
  }
  if (state_->pool) {
    state_->pool->close();
  }
}

} // namespace instantee::export_apple
