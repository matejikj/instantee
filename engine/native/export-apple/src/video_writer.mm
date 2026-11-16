#include <instantee/export_apple/video_writer.hpp>

#include "pixel_buffer_native.hpp"

#include <AVFoundation/AVFoundation.h>
#include <CoreMedia/CoreMedia.h>

#include <algorithm>
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

[[nodiscard]] std::expected<core::RationalTime, Error>
from_cm_time(CMTime time, const char *invalid_code) noexcept {
  if (!CMTIME_IS_NUMERIC(time) || time.value < 0 || time.timescale <= 0) {
    return std::unexpected(Error{ExitCode::export_failure, invalid_code, kPhase,
                                 "Media timing value is not a numeric CMTime"});
  }
  return core::RationalTime::make(static_cast<std::uint64_t>(time.value),
                                  static_cast<std::uint64_t>(time.timescale));
}

} // namespace

struct VideoWriter::Impl {
  VideoWriterConfig config{};
  AVAssetWriter *writer = nil;
  AVAssetWriterInput *input = nil;
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

    // Disable frame reordering so decode order matches presentation order and
    // every appended PTS lands in the finished MP4 without B-frame delay.
    NSDictionary *compression = @{
      AVVideoAverageBitRateKey : @(6 * 1000 * 1000),
      AVVideoProfileLevelKey : AVVideoProfileLevelH264HighAutoLevel,
      AVVideoAllowFrameReorderingKey : @NO
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
  const auto frame_duration = impl_->clock.pts(1);
  if (!frame_duration) {
    return std::unexpected(frame_duration.error());
  }
  if (pts->numerator >
          static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max()) ||
      pts->denominator >
          static_cast<std::uint64_t>(std::numeric_limits<int32_t>::max()) ||
      frame_duration->numerator >
          static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max()) ||
      frame_duration->denominator >
          static_cast<std::uint64_t>(std::numeric_limits<int32_t>::max())) {
    return std::unexpected(
        Error{ExitCode::invalid_input, "pts_out_of_range", kPhase,
              "Presentation timestamp exceeds CMTime range"});
  }

  // appendPixelBuffer leaves sample duration invalid, so the last frame would
  // truncate declared media length. Explicit one-frame duration keeps asset
  // duration equal to FrameClock::duration(frame_count).
  BOOL ok = NO;
  @try {
    CMVideoFormatDescriptionRef format = nullptr;
    const OSStatus format_status = CMVideoFormatDescriptionCreateForImageBuffer(
        kCFAllocatorDefault, native, &format);
    if (format_status != noErr || format == nullptr) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "format_description_failed", kPhase,
                                   "Cannot describe pixel buffer for sample"});
    }

    CMSampleTimingInfo timing{};
    timing.duration = to_cm_time(*frame_duration);
    timing.presentationTimeStamp = to_cm_time(*pts);
    timing.decodeTimeStamp = kCMTimeInvalid;

    CMSampleBufferRef sample = nullptr;
    const OSStatus sample_status = CMSampleBufferCreateForImageBuffer(
        kCFAllocatorDefault, native, true, nullptr, nullptr, format, &timing,
        &sample);
    CFRelease(format);
    if (sample_status != noErr || sample == nullptr) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "sample_buffer_failed", kPhase,
                                   "Cannot create timed sample buffer"});
    }

    ok = [impl_->input appendSampleBuffer:sample];
    CFRelease(sample);
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

std::expected<PresentationTimeline, Error>
extract_presentation_timeline(const std::filesystem::path &path) {
  if (path.empty() || !std::filesystem::exists(path)) {
    return std::unexpected(Error{ExitCode::invalid_input, "missing_media",
                                 kPhase, "Media file does not exist"});
  }

  @try {
    NSURL *url = [NSURL fileURLWithPath:@(path.c_str()) isDirectory:NO];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];

    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block NSArray<AVAssetTrack *> *tracks = nil;
    [asset
        loadTracksWithMediaType:AVMediaTypeVideo
              completionHandler:^(NSArray<AVAssetTrack *> *loaded, NSError *) {
                @try {
                  tracks = loaded;
                } @catch (...) {
                  tracks = nil;
                }
                dispatch_semaphore_signal(done);
              }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (tracks.count != 1) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "timeline_track_count", kPhase,
                                   "Expected exactly one video track"});
    }

    NSError *error = nil;
    AVAssetReader *reader = [[AVAssetReader alloc] initWithAsset:asset
                                                           error:&error];
    if (reader == nil) {
      return std::unexpected(
          Error{ExitCode::export_failure, "timeline_reader_failed", kPhase,
                "Cannot open media for timeline extraction"});
    }

    // Decode to BGRA so each output sample is one presented frame with a
    // stable presentation timestamp (compressed decode-order is unsuitable).
    NSDictionary *settings =
        @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    AVAssetReaderTrackOutput *output =
        [[AVAssetReaderTrackOutput alloc] initWithTrack:tracks.firstObject
                                         outputSettings:settings];
    output.alwaysCopiesSampleData = NO;
    if (![reader canAddOutput:output]) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "timeline_output_failed", kPhase,
                                   "Cannot attach timeline reader output"});
    }
    [reader addOutput:output];
    if (![reader startReading]) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "timeline_start_failed", kPhase,
                                   "Timeline reader failed to start"});
    }

    PresentationTimeline timeline{};
    for (;;) {
      CMSampleBufferRef sample = [output copyNextSampleBuffer];
      if (sample == nullptr) {
        break;
      }
      CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
      if (!CMTIME_IS_NUMERIC(pts)) {
        pts = CMSampleBufferGetOutputPresentationTimeStamp(sample);
      }
      CFRelease(sample);
      // Non-media control samples can appear without a presentation time.
      if (!CMTIME_IS_NUMERIC(pts)) {
        continue;
      }
      auto rational = from_cm_time(pts, "timeline_pts_invalid");
      if (!rational) {
        return std::unexpected(rational.error());
      }
      timeline.presentation_timestamps.push_back(*rational);
    }

    if (reader.status != AVAssetReaderStatusCompleted) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "timeline_read_failed", kPhase,
                                   "Timeline reader did not complete"});
    }

    // Compressed readers may yield decode order; presentation validation needs
    // ascending PTS with no duplicates.
    std::sort(timeline.presentation_timestamps.begin(),
              timeline.presentation_timestamps.end());
    for (std::size_t index = 1; index < timeline.presentation_timestamps.size();
         ++index) {
      if (!(timeline.presentation_timestamps[index] >
            timeline.presentation_timestamps[index - 1U])) {
        return std::unexpected(Error{ExitCode::export_failure,
                                     "timeline_not_monotonic", kPhase,
                                     "Extracted presentation times contain "
                                     "duplicates or are not increasing"});
      }
    }

    auto duration = from_cm_time(asset.duration, "timeline_duration_invalid");
    if (!duration) {
      return std::unexpected(duration.error());
    }
    timeline.asset_duration = *duration;
    return timeline;
  } @catch (...) {
    return std::unexpected(Error{ExitCode::export_failure, "timeline_exception",
                                 kPhase,
                                 "Exception while extracting presentation "
                                 "timeline"});
  }
}

std::expected<void, Error>
validate_presentation_timeline(const PresentationTimeline &timeline,
                               const core::FrameClock &clock,
                               core::FrameIndex expected_frame_count) {
  if (expected_frame_count == 0U) {
    return std::unexpected(Error{ExitCode::invalid_input, "empty_timeline",
                                 kPhase,
                                 "Expected frame count must be non-zero"});
  }
  if (timeline.presentation_timestamps.size() != expected_frame_count) {
    return std::unexpected(Error{ExitCode::export_failure,
                                 "frame_count_mismatch", kPhase,
                                 "Extracted frame count does not match the "
                                 "expected media length"});
  }

  const auto expected_duration = clock.duration(expected_frame_count);
  if (!expected_duration) {
    return std::unexpected(expected_duration.error());
  }
  if (timeline.asset_duration != *expected_duration) {
    return std::unexpected(Error{ExitCode::export_failure, "duration_mismatch",
                                 kPhase,
                                 "Declared media duration does not match the "
                                 "frame clock"});
  }

  for (core::FrameIndex index = 0; index < expected_frame_count; ++index) {
    const auto expected_pts = clock.pts(index);
    if (!expected_pts) {
      return std::unexpected(expected_pts.error());
    }
    const auto &actual = timeline.presentation_timestamps[index];
    if (actual != *expected_pts) {
      return std::unexpected(Error{ExitCode::export_failure, "pts_mismatch",
                                   kPhase,
                                   "Presentation timestamp does not match the "
                                   "exact frame clock"});
    }
  }

  if (timeline.presentation_timestamps.front() != core::RationalTime{0, 1}) {
    return std::unexpected(Error{ExitCode::export_failure, "first_pts_mismatch",
                                 kPhase,
                                 "First presentation timestamp must be "
                                 "zero"});
  }
  const auto last_expected = clock.pts(expected_frame_count - 1U);
  if (!last_expected) {
    return std::unexpected(last_expected.error());
  }
  if (timeline.presentation_timestamps.back() != *last_expected) {
    return std::unexpected(Error{ExitCode::export_failure, "last_pts_mismatch",
                                 kPhase,
                                 "Last presentation timestamp does not match "
                                 "the final frame index"});
  }
  return {};
}

} // namespace instantee::spike::apple
