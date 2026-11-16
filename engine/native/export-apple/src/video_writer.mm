#include <instantee/export_apple/video_writer.hpp>

#include "pixel_buffer_native.hpp"

#include <AVFoundation/AVFoundation.h>
#include <CoreMedia/CoreMedia.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <limits>
#include <mutex>
#include <new>
#include <utility>

#if !__has_feature(objc_arc)
#error "The Apple export adapter requires ARC"
#endif

namespace instantee::spike::apple {
namespace {

constexpr const char *kPhase = "video_writer";

[[nodiscard]] CMTime to_cm_time(core::RationalTime pts) noexcept {
  return CMTimeMake(static_cast<int64_t>(pts.numerator),
                    static_cast<int32_t>(pts.denominator));
}

} // namespace

struct VideoWriter::Impl {
  VideoWriterConfig config{};
  AVAssetWriter *writer = nil;
  AVAssetWriterInput *input = nil;
  AVAssetWriterInputPixelBufferAdaptor *adaptor = nil;
  core::FrameClock clock = core::FrameClock::media_30_1();

  std::mutex mutex;
  std::condition_variable_any readiness;
  bool cancelled = false;
  bool finished = false;
  bool failed = false;
  bool finishing = false;

  void notify_ready_unlocked() noexcept { readiness.notify_all(); }

  // Marks cancel under the mutex and returns the writer to cancel outside the
  // lock. AVAssetWriter cancelWriting must not run while this mutex is held.
  [[nodiscard]] AVAssetWriter *mark_cancelled_unlocked() noexcept {
    if (cancelled || finished) {
      return nil;
    }
    cancelled = true;
    notify_ready_unlocked();
    return writer;
  }

  void cancel_writer(AVAssetWriter *writer_to_cancel) noexcept {
    if (writer_to_cancel == nil) {
      return;
    }
    @try {
      if (writer_to_cancel.status == AVAssetWriterStatusWriting) {
        [writer_to_cancel cancelWriting];
      }
    } @catch (...) {
      // Native teardown must not escape exceptions.
    }
  }
};

VideoWriter::VideoWriter(std::shared_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

VideoWriter::VideoWriter(VideoWriter &&) noexcept = default;
VideoWriter &VideoWriter::operator=(VideoWriter &&) noexcept = default;

VideoWriter::~VideoWriter() { cancel(); }

std::expected<VideoWriter, Error>
VideoWriter::create(VideoWriterConfig config) {
  if (config.output_path.empty() || !config.format.valid() ||
      !config.frame_rate.valid()) {
    return std::unexpected(Error{ExitCode::invalid_input,
                                 "invalid_writer_config", kPhase,
                                 "Video writer configuration is invalid"});
  }
  if (std::filesystem::exists(config.output_path)) {
    return std::unexpected(Error{ExitCode::invalid_input, "output_exists",
                                 kPhase,
                                 "Video writer output path must not exist"});
  }

  auto clock = core::FrameClock::create(config.frame_rate);
  if (!clock) {
    return std::unexpected(clock.error());
  }

  auto impl = std::shared_ptr<Impl>(new (std::nothrow) Impl{});
  if (!impl) {
    return std::unexpected(Error{ExitCode::export_failure, "out_of_memory",
                                 kPhase, "Cannot allocate video writer"});
  }
  impl->config = std::move(config);
  impl->clock = *std::move(clock);

  @try {
    NSError *error = nil;
    NSURL *url = [NSURL fileURLWithPath:@(impl->config.output_path.c_str())
                            isDirectory:NO];
    impl->writer = [[AVAssetWriter alloc] initWithURL:url
                                             fileType:AVFileTypeMPEG4
                                                error:&error];
    if (impl->writer == nil) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "writer_create_failed", kPhase,
                                   "Cannot create AVAssetWriter"});
    }

    NSDictionary *compression = @{
      AVVideoAverageBitRateKey : @(6 * 1000 * 1000),
      AVVideoProfileLevelKey : AVVideoProfileLevelH264HighAutoLevel
    };
    NSDictionary *settings = @{
      AVVideoCodecKey : AVVideoCodecTypeH264,
      AVVideoWidthKey : @(impl->config.format.width),
      AVVideoHeightKey : @(impl->config.format.height),
      AVVideoCompressionPropertiesKey : compression
    };
    impl->input = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo
                                                 outputSettings:settings];
    impl->input.expectsMediaDataInRealTime = NO;
    if (![impl->writer canAddInput:impl->input]) {
      return std::unexpected(Error{ExitCode::export_failure, "cannot_add_input",
                                   kPhase,
                                   "Cannot add video-only writer input"});
    }
    [impl->writer addInput:impl->input];

    NSDictionary *source = @{
      (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
      (id)kCVPixelBufferWidthKey : @(impl->config.format.width),
      (id)kCVPixelBufferHeightKey : @(impl->config.format.height)
    };
    impl->adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc]
           initWithAssetWriterInput:impl->input
        sourcePixelBufferAttributes:source];

    if (![impl->writer startWriting]) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "start_writing_failed", kPhase,
                                   "AVAssetWriter failed to start"});
    }
    [impl->writer startSessionAtSourceTime:kCMTimeZero];
  } @catch (...) {
    return std::unexpected(Error{ExitCode::export_failure, "writer_exception",
                                 kPhase,
                                 "Exception while creating AVAssetWriter"});
  }

  return VideoWriter{std::move(impl)};
}

const std::filesystem::path &VideoWriter::output_path() const noexcept {
  return impl_->config.output_path;
}

PixelBufferFormat VideoWriter::format() const noexcept {
  return impl_ ? impl_->config.format : PixelBufferFormat{};
}

bool VideoWriter::cancelled() const noexcept {
  return impl_ && impl_->cancelled;
}

bool VideoWriter::finished() const noexcept { return impl_ && impl_->finished; }

std::expected<void, Error> VideoWriter::wait_until_ready(std::stop_token stop) {
  if (!impl_) {
    return std::unexpected(Error{ExitCode::export_failure, "writer_not_ready",
                                 kPhase, "Video writer is not ready"});
  }

  // Poll readiness with stop-aware timed waits. A requestMediaDataWhenReady
  // pump belongs to producer-driven encoding; using it only as a signal can
  // busy-loop when the block does not supply media.
  for (;;) {
    AVAssetWriterInput *input = nil;
    {
      std::unique_lock lock(impl_->mutex);
      if (stop.stop_requested() || impl_->cancelled) {
        return std::unexpected(
            Error{ExitCode::invalid_input, "cancelled", kPhase,
                  "Video writer readiness wait was cancelled"});
      }
      if (impl_->failed) {
        return std::unexpected(Error{ExitCode::export_failure, "writer_failed",
                                     kPhase,
                                     "AVAssetWriter entered a failed state"});
      }
      if (impl_->finished) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "already_finished", kPhase,
                                     "Video writer already finished"});
      }
      input = impl_->input;
    }

    BOOL ready = NO;
    @try {
      ready = input != nil && input.readyForMoreMediaData;
    } @catch (...) {
      std::lock_guard lock(impl_->mutex);
      impl_->failed = true;
      return std::unexpected(
          Error{ExitCode::export_failure, "readiness_exception", kPhase,
                "Exception while querying writer readiness"});
    }
    if (ready) {
      return {};
    }

    std::unique_lock lock(impl_->mutex);
    const auto wake = [&] {
      return impl_->cancelled || impl_->failed || impl_->finished ||
             stop.stop_requested();
    };
    impl_->readiness.wait_for(lock, stop, std::chrono::milliseconds(5), wake);
  }
}

std::expected<void, Error> VideoWriter::append(PixelBuffer &buffer,
                                               core::FrameIndex frame_index,
                                               std::stop_token stop) {
  if (!impl_) {
    return std::unexpected(Error{ExitCode::export_failure, "writer_not_ready",
                                 kPhase, "Video writer is not ready"});
  }
  if (detail::pixel_buffer_empty(buffer)) {
    return std::unexpected(Error{ExitCode::invalid_input, "empty_buffer",
                                 kPhase,
                                 "Cannot append an empty pixel buffer"});
  }
  if (buffer.format().width != impl_->config.format.width ||
      buffer.format().height != impl_->config.format.height) {
    return std::unexpected(Error{ExitCode::invalid_input, "format_mismatch",
                                 kPhase,
                                 "Pixel buffer format does not match writer"});
  }

  // Snapshot the native handle before readiness wait so a failure leaves the
  // caller's buffer intact and unsealed.
  CVPixelBufferRef native = detail::borrow_cv_pixel_buffer(buffer);
  if (native == nullptr) {
    return std::unexpected(Error{ExitCode::export_failure, "buffer_not_ready",
                                 kPhase, "Pixel buffer native handle missing"});
  }

  if (auto ready = wait_until_ready(stop); !ready) {
    return ready;
  }

  const auto pts = impl_->clock.pts(frame_index);
  if (!pts) {
    return std::unexpected(pts.error());
  }
  if (pts->numerator >
          static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max()) ||
      pts->denominator >
          static_cast<std::uint64_t>(std::numeric_limits<int32_t>::max())) {
    return std::unexpected(
        Error{ExitCode::invalid_input, "pts_out_of_range", kPhase,
              "Presentation timestamp exceeds CMTime range"});
  }

  BOOL ok = NO;
  @try {
    ok = [impl_->adaptor appendPixelBuffer:native
                      withPresentationTime:to_cm_time(*pts)];
  } @catch (...) {
    return std::unexpected(Error{ExitCode::export_failure, "append_exception",
                                 kPhase,
                                 "Exception while appending a pixel buffer"});
  }
  if (!ok) {
    return std::unexpected(Error{ExitCode::export_failure, "append_failed",
                                 kPhase,
                                 "AVAssetWriter failed to append a frame"});
  }

  buffer.seal();
  PixelBuffer consumed = std::move(buffer);
  static_cast<void>(consumed);
  return {};
}

std::expected<void, Error> VideoWriter::finish() {
  if (!impl_) {
    return std::unexpected(Error{ExitCode::export_failure, "writer_not_ready",
                                 kPhase, "Video writer is not ready"});
  }

  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->cancelled) {
      return std::unexpected(Error{ExitCode::invalid_input, "cancelled", kPhase,
                                   "Cannot finish a cancelled video writer"});
    }
    if (impl_->finished) {
      return {};
    }
    if (impl_->finishing) {
      return std::unexpected(Error{ExitCode::invalid_input,
                                   "finish_in_progress", kPhase,
                                   "Video writer finish already started"});
    }
    impl_->finishing = true;
  }

  auto done = std::make_shared<std::promise<std::expected<void, Error>>>();
  auto future = done->get_future();
  std::weak_ptr<Impl> weak_self = impl_;

  @try {
    [impl_->input markAsFinished];
    [impl_->writer finishWritingWithCompletionHandler:^{
      @try {
        auto self = weak_self.lock();
        std::expected<void, Error> result{};
        if (!self || self->writer.status != AVAssetWriterStatusCompleted) {
          result = std::unexpected(
              Error{ExitCode::export_failure, "finish_failed", kPhase,
                    "AVAssetWriter finish did not complete successfully"});
        }
        if (self) {
          std::lock_guard lock(self->mutex);
          if (result) {
            self->finished = true;
          } else {
            self->failed = true;
          }
          self->notify_ready_unlocked();
        }
        try {
          done->set_value(result);
        } catch (...) {
        }
      } @catch (...) {
        if (auto self = weak_self.lock()) {
          std::lock_guard lock(self->mutex);
          self->failed = true;
          self->notify_ready_unlocked();
        }
        try {
          done->set_value(std::unexpected(
              Error{ExitCode::export_failure, "finish_exception", kPhase,
                    "Exception in finishWriting completion handler"}));
        } catch (...) {
        }
      }
    }];
  } @catch (...) {
    std::lock_guard lock(impl_->mutex);
    impl_->failed = true;
    return std::unexpected(Error{ExitCode::export_failure, "finish_exception",
                                 kPhase,
                                 "Exception while finishing AVAssetWriter"});
  }

  return future.get();
}

void VideoWriter::cancel() noexcept {
  if (!impl_) {
    return;
  }
  AVAssetWriter *writer_to_cancel = nil;
  {
    std::lock_guard lock(impl_->mutex);
    writer_to_cancel = impl_->mark_cancelled_unlocked();
  }
  impl_->cancel_writer(writer_to_cancel);
}

std::expected<MediaInspection, Error>
inspect_media(const std::filesystem::path &path) {
  if (path.empty() || !std::filesystem::exists(path)) {
    return std::unexpected(Error{ExitCode::invalid_input, "missing_media",
                                 kPhase, "Media file does not exist"});
  }

  @try {
    NSURL *url = [NSURL fileURLWithPath:@(path.c_str()) isDirectory:NO];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];

    auto load_tracks = [&](AVMediaType type) -> NSArray<AVAssetTrack *> * {
      dispatch_semaphore_t done = dispatch_semaphore_create(0);
      __block NSArray<AVAssetTrack *> *tracks = nil;
      [asset loadTracksWithMediaType:type
                   completionHandler:^(NSArray<AVAssetTrack *> *loaded,
                                       NSError *) {
                     @try {
                       tracks = loaded;
                     } @catch (...) {
                       tracks = nil;
                     }
                     dispatch_semaphore_signal(done);
                   }];
      dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
      return tracks;
    };

    NSArray<AVAssetTrack *> *video = load_tracks(AVMediaTypeVideo);
    NSArray<AVAssetTrack *> *audio = load_tracks(AVMediaTypeAudio);

    MediaInspection info{};
    info.playable = asset.playable;
    info.video_track_count = static_cast<std::uint32_t>(video.count);
    info.audio_track_count = static_cast<std::uint32_t>(audio.count);

    const CMTime duration = asset.duration;
    if (CMTIME_IS_NUMERIC(duration) && duration.timescale > 0) {
      info.duration_numerator = duration.value;
      info.duration_denominator = duration.timescale;
    }

    if (info.video_track_count > 0) {
      AVAssetTrack *track = video.firstObject;
      const CGSize size = track.naturalSize;
      info.width = static_cast<std::uint32_t>(size.width);
      info.height = static_cast<std::uint32_t>(size.height);
      info.nominal_frame_rate = static_cast<double>(track.nominalFrameRate);
      if (track.formatDescriptions.count > 0) {
        const auto desc = (__bridge CMFormatDescriptionRef)
                              track.formatDescriptions.firstObject;
        info.h264 =
            CMFormatDescriptionGetMediaSubType(desc) == kCMVideoCodecType_H264;
      }
    }
    return info;
  } @catch (...) {
    return std::unexpected(Error{ExitCode::export_failure, "inspect_exception",
                                 kPhase, "Exception while inspecting media"});
  }
}

} // namespace instantee::spike::apple
