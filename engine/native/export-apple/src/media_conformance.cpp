#include <instantee/export_apple/media_conformance.hpp>

#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace instantee::spike::apple {
namespace {

using diagnostics::Error;
using diagnostics::ErrorKind;
using diagnostics::ExitCode;

constexpr std::string_view kPhase = "media_conformance";

[[nodiscard]] Error make_export(std::string_view code,
                                std::string_view message) {
  return Error{ExitCode::export_failure, ErrorKind::export_failure, code,
               kPhase, message};
}

[[nodiscard]] Error make_invalid(std::string_view code,
                                 std::string_view message) {
  return Error{ExitCode::invalid_input, ErrorKind::invalid_input, code, kPhase,
               message};
}

} // namespace

std::expected<MediaConformanceReport, Error>
validate_mp4_conformance(const std::filesystem::path &path,
                         const MediaConformanceExpect &expect) {
  if (path.empty()) {
    return std::unexpected(
        make_invalid("conformance.path_empty", "Media path is empty"));
  }
  if (expect.frame_count == core::FrameIndex{0}) {
    return std::unexpected(make_invalid(
        "conformance.empty_expect",
        "Expected frame count must be non-zero for media conformance"));
  }
  if (expect.nominal_frame_rate_tolerance < 0.0) {
    return std::unexpected(
        make_invalid("conformance.tolerance_invalid",
                     "Nominal frame-rate tolerance must be non-negative"));
  }

  auto inspection = inspect_media(path);
  if (!inspection) {
    return std::unexpected(inspection.error());
  }

  if (expect.require_playable && !inspection->playable) {
    return std::unexpected(make_export(
        "conformance.not_playable",
        "Media asset is not marked playable by the platform inspector"));
  }
  if (expect.require_h264 && !inspection->h264) {
    return std::unexpected(
        make_export("conformance.not_h264", "Expected an H.264 video track"));
  }
  if (expect.require_single_video_track &&
      inspection->video_track_count != 1U) {
    return std::unexpected(
        make_export("conformance.video_track_count",
                    std::format("Expected exactly one video track, found {}",
                                inspection->video_track_count)));
  }
  if (expect.require_no_audio && inspection->audio_track_count != 0U) {
    return std::unexpected(
        make_export("conformance.audio_present",
                    std::format("Expected no audio track, found {}",
                                inspection->audio_track_count)));
  }
  if (inspection->width != expect.width) {
    return std::unexpected(make_export(
        "conformance.width", std::format("Expected width {}, found {}",
                                         expect.width, inspection->width)));
  }
  if (inspection->height != expect.height) {
    return std::unexpected(make_export(
        "conformance.height", std::format("Expected height {}, found {}",
                                          expect.height, inspection->height)));
  }
  if (expect.require_rec709) {
    if (!inspection->rec709_primaries) {
      return std::unexpected(
          make_export("conformance.color_primaries",
                      "Expected ITU-R BT.709 color primaries metadata"));
    }
    if (!inspection->rec709_transfer) {
      return std::unexpected(
          make_export("conformance.color_transfer",
                      "Expected ITU-R BT.709 transfer-function metadata"));
    }
    if (!inspection->rec709_ycbcr_matrix) {
      return std::unexpected(
          make_export("conformance.color_matrix",
                      "Expected ITU-R BT.709 YCbCr matrix metadata"));
    }
  }
  if (std::abs(inspection->nominal_frame_rate - expect.nominal_frame_rate) >
      expect.nominal_frame_rate_tolerance) {
    return std::unexpected(
        make_export("conformance.frame_rate",
                    std::format("Expected nominal frame rate {}±{}, found {}",
                                expect.nominal_frame_rate,
                                expect.nominal_frame_rate_tolerance,
                                inspection->nominal_frame_rate)));
  }

  MediaConformanceReport report;
  report.inspection = *inspection;

  if (!expect.decode_timeline) {
    return report;
  }

  auto timeline = extract_presentation_timeline(path);
  if (!timeline) {
    return std::unexpected(timeline.error());
  }
  if (auto pts = validate_presentation_timeline(*timeline, expect.clock,
                                                expect.frame_count);
      !pts) {
    return std::unexpected(pts.error());
  }
  report.timeline = std::move(*timeline);
  return report;
}

} // namespace instantee::spike::apple
