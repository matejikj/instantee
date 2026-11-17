#include <instantee/core_model/time_convert.hpp>

#include <limits>
#include <string_view>

namespace instantee::core {
namespace {

diagnostics::Error bad_time(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input,
                            "invalid_time_conversion", "time", message};
}

[[nodiscard]] bool pts_matches(FrameIndex frame, RationalTime seconds,
                               FrameRate rate) noexcept {
  const unsigned __int128 left =
      static_cast<unsigned __int128>(frame.value()) *
      static_cast<unsigned __int128>(rate.denominator()) *
      static_cast<unsigned __int128>(seconds.denominator());
  const unsigned __int128 right =
      static_cast<unsigned __int128>(seconds.numerator()) *
      static_cast<unsigned __int128>(rate.numerator());
  return left == right;
}

} // namespace

std::expected<FrameConversion, diagnostics::Error>
seconds_to_frame(RationalTime seconds, FrameRate rate) noexcept {
  if (!rate.valid()) {
    return std::unexpected(bad_time("Invalid frame rate"));
  }
  if (seconds.denominator() == 0U) {
    return std::unexpected(bad_time("Invalid RationalTime"));
  }

  // frame = round(sec_num * fps_num / (sec_den * fps_den)) half-away-from-zero
  const unsigned __int128 product =
      static_cast<unsigned __int128>(seconds.numerator()) *
      static_cast<unsigned __int128>(rate.numerator());
  const unsigned __int128 scale =
      static_cast<unsigned __int128>(seconds.denominator()) *
      static_cast<unsigned __int128>(rate.denominator());
  if (scale == 0) {
    return std::unexpected(bad_time("Invalid conversion scale"));
  }
  if (product > (std::numeric_limits<unsigned __int128>::max() - scale) / 2U) {
    return std::unexpected(bad_time("Conversion overflow"));
  }
  const unsigned __int128 rounded = (product * 2U + scale) / (scale * 2U);
  if (rounded > std::numeric_limits<std::uint64_t>::max()) {
    return std::unexpected(bad_time("Frame index exceeds uint64"));
  }
  FrameConversion out;
  out.frame = FrameIndex{static_cast<std::uint64_t>(rounded)};
  out.sub_frame_loss = !pts_matches(out.frame, seconds, rate);
  return out;
}

std::expected<FrameConversion, diagnostics::Error>
seconds_micros_to_frame(std::uint64_t microseconds, FrameRate rate) noexcept {
  auto seconds = RationalTime::make(microseconds, 1'000'000ULL);
  if (!seconds) {
    return std::unexpected(seconds.error());
  }
  return seconds_to_frame(*seconds, rate);
}

std::expected<FrameInterval, diagnostics::Error>
seconds_interval_to_frames(RationalTime start, RationalTime end,
                           FrameRate rate) noexcept {
  if ((start <=> end) > 0) {
    return std::unexpected(bad_time("Interval end precedes start"));
  }
  auto start_frame = seconds_to_frame(start, rate);
  if (!start_frame) {
    return std::unexpected(start_frame.error());
  }
  auto end_frame = seconds_to_frame(end, rate);
  if (!end_frame) {
    return std::unexpected(end_frame.error());
  }
  FrameInterval out;
  out.start = start_frame->frame;
  out.end_exclusive = end_frame->frame.value() < start_frame->frame.value()
                          ? start_frame->frame
                          : end_frame->frame;
  out.sub_frame_loss = start_frame->sub_frame_loss || end_frame->sub_frame_loss;
  return out;
}

} // namespace instantee::core
