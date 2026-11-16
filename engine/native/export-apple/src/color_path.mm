#include <instantee/export_apple/color_path.hpp>

#include <AVFoundation/AVFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>

#include <cstring>
#include <new>

#if !__has_feature(objc_arc)
#error "The Apple export adapter requires ARC"
#endif

namespace instantee::spike::apple {
namespace {

constexpr const char *kPhase = "color_path";

} // namespace

std::expected<void, Error> flatten_straight_rgba8_to_opaque_bgra8(
    std::span<const std::uint8_t> straight_rgba,
    std::span<std::uint8_t> opaque_bgra) noexcept {
  if (straight_rgba.size() != opaque_bgra.size() ||
      (straight_rgba.size() % 4U) != 0U) {
    return std::unexpected(
        Error{ExitCode::invalid_input, "size_mismatch", kPhase,
              "RGBA/BGRA payloads must share a multiple-of-4 "
              "byte count"});
  }

  for (std::size_t index = 0; index < straight_rgba.size(); index += 4U) {
    const Rgba8Color rgba{straight_rgba[index], straight_rgba[index + 1U],
                          straight_rgba[index + 2U], straight_rgba[index + 3U]};
    const Bgra8Color bgra = flatten_straight_rgba8_over_black(rgba);
    opaque_bgra[index] = bgra.b;
    opaque_bgra[index + 1U] = bgra.g;
    opaque_bgra[index + 2U] = bgra.r;
    opaque_bgra[index + 3U] = bgra.a;
  }
  return {};
}

std::expected<std::vector<DecodedBgraFrame>, Error>
decode_video_frames_bgra(const std::filesystem::path &path) {
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
                                   "color_track_count", kPhase,
                                   "Expected exactly one video track"});
    }

    NSError *error = nil;
    AVAssetReader *reader = [[AVAssetReader alloc] initWithAsset:asset
                                                           error:&error];
    if (reader == nil) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "color_reader_failed", kPhase,
                                   "Cannot open media for color decode"});
    }

    NSDictionary *settings =
        @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    AVAssetReaderTrackOutput *output =
        [[AVAssetReaderTrackOutput alloc] initWithTrack:tracks.firstObject
                                         outputSettings:settings];
    output.alwaysCopiesSampleData = NO;
    if (![reader canAddOutput:output]) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "color_output_failed", kPhase,
                                   "Cannot attach color reader output"});
    }
    [reader addOutput:output];
    if (![reader startReading]) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "color_start_failed", kPhase,
                                   "Color reader failed to start"});
    }

    std::vector<DecodedBgraFrame> frames;
    for (;;) {
      CMSampleBufferRef sample = [output copyNextSampleBuffer];
      if (sample == nullptr) {
        break;
      }

      CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
      if (image == nullptr) {
        CFRelease(sample);
        continue;
      }

      if (CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) !=
          kCVReturnSuccess) {
        CFRelease(sample);
        return std::unexpected(Error{ExitCode::export_failure,
                                     "color_lock_failed", kPhase,
                                     "Cannot lock decoded pixel buffer"});
      }

      const auto width =
          static_cast<std::uint32_t>(CVPixelBufferGetWidth(image));
      const auto height =
          static_cast<std::uint32_t>(CVPixelBufferGetHeight(image));
      const auto bytes_per_row =
          static_cast<std::size_t>(CVPixelBufferGetBytesPerRow(image));
      const auto tight_row = static_cast<std::size_t>(width) * 4U;
      auto *base =
          static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddress(image));
      if (base == nullptr || width == 0U || height == 0U ||
          bytes_per_row < tight_row) {
        CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
        CFRelease(sample);
        return std::unexpected(Error{ExitCode::export_failure,
                                     "color_base_address_failed", kPhase,
                                     "Decoded pixel buffer layout is invalid"});
      }

      DecodedBgraFrame frame{};
      frame.format = PixelBufferFormat{width, height};
      frame.bgra.resize(static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height) * 4U);
      for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(frame.bgra.data() + static_cast<std::size_t>(y) * tight_row,
                    base + static_cast<std::size_t>(y) * bytes_per_row,
                    tight_row);
      }

      CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
      CFRelease(sample);
      frames.push_back(std::move(frame));
    }

    if (reader.status != AVAssetReaderStatusCompleted) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "color_read_failed", kPhase,
                                   "Color reader did not complete"});
    }
    if (frames.empty()) {
      return std::unexpected(Error{ExitCode::export_failure, "color_no_frames",
                                   kPhase,
                                   "Decoded media contained no frames"});
    }
    return frames;
  } @catch (...) {
    return std::unexpected(Error{ExitCode::export_failure, "color_exception",
                                 kPhase,
                                 "Exception while decoding video frames"});
  }
}

} // namespace instantee::spike::apple
