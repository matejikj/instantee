#include <instantee/direction/direction_report.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "direction_report", message};
}

[[nodiscard]] std::string reason_for_source(TrackSource source) {
  switch (source) {
  case TrackSource::explicit_author:
    return "explicit_author_wins";
  case TrackSource::shot_override:
    return "shot_override_wins";
  case TrackSource::style_bible:
    return "style_bible_wins";
  case TrackSource::framework_default:
    return "framework_default_wins";
  }
  return "unknown_source";
}

[[nodiscard]] double interpolate_value(const std::vector<DirectedKeyframe> &kfs,
                                       std::uint64_t frame) {
  if (kfs.empty()) {
    return 0.0;
  }
  if (frame <= kfs.front().frame) {
    return kfs.front().value;
  }
  for (std::size_t i = 1; i < kfs.size(); ++i) {
    const DirectedKeyframe &left = kfs[i - 1];
    const DirectedKeyframe &right = kfs[i];
    if (frame > right.frame) {
      continue;
    }
    if (right.frame == left.frame) {
      return right.value;
    }
    const double t = static_cast<double>(frame - left.frame) /
                     static_cast<double>(right.frame - left.frame);
    return left.value + (right.value - left.value) * t;
  }
  return kfs.back().value;
}

[[nodiscard]] bool channel_covers_frame(const ChannelDecision &channel,
                                        std::uint64_t frame) noexcept {
  if (channel.keyframes.empty()) {
    return false;
  }
  return frame >= channel.keyframes.front().frame &&
         frame <= channel.keyframes.back().frame;
}

void append_escaped(std::ostringstream &out, std::string_view text) {
  for (char ch : text) {
    if (ch == '\\' || ch == '"' || ch == '\n' || ch == '\r') {
      out << '\\';
      if (ch == '\n') {
        out << 'n';
      } else if (ch == '\r') {
        out << 'r';
      } else {
        out << ch;
      }
    } else {
      out << ch;
    }
  }
}

} // namespace

void DirectionReport::add_intent(IntentDecision decision) {
  intents_.push_back(std::move(decision));
}

void DirectionReport::add_channel(ChannelDecision decision) {
  channels_.push_back(std::move(decision));
}

void DirectionReport::add_transition(TransitionDecision decision) {
  transitions_.push_back(std::move(decision));
}

void DirectionReport::add_warning(DirectionWarning warning) {
  warnings_.push_back(std::move(warning));
}

std::expected<DirectionExplanation, diagnostics::Error>
DirectionReport::explain(std::string_view object_id,
                         std::uint64_t frame) const {
  if (object_id.empty()) {
    return std::unexpected(
        make_error("invalid_object_id", "Object id is required for explain"));
  }

  std::vector<const ChannelDecision *> matches;
  for (const ChannelDecision &channel : channels_) {
    if (channel.object_id == object_id) {
      matches.push_back(&channel);
    }
  }
  if (matches.empty()) {
    return std::unexpected(
        make_error("no_direction_for_object",
                   "Direction report has no channel decision for the object"));
  }

  const ChannelDecision *chosen = nullptr;
  for (const ChannelDecision *channel : matches) {
    if (channel->property == "opacity" &&
        channel_covers_frame(*channel, frame)) {
      chosen = channel;
      break;
    }
  }
  if (chosen == nullptr) {
    for (const ChannelDecision *channel : matches) {
      if (channel_covers_frame(*channel, frame)) {
        chosen = channel;
        break;
      }
    }
  }
  if (chosen == nullptr) {
    // Fall back to the first channel so callers can still explain the object
    // even when the queried frame sits outside sparse keyframe spans.
    chosen = matches.front();
  }

  DirectionExplanation out;
  out.object_id = std::string(object_id);
  out.frame = frame;
  out.shot_id = chosen->shot_id;
  out.property = chosen->property;
  out.track_id = chosen->track_id;
  out.source = chosen->source;
  out.reason_code = chosen->reason_code;
  out.value = interpolate_value(chosen->keyframes, frame);

  for (const IntentDecision &intent : intents_) {
    if (intent.shot_id == chosen->shot_id) {
      out.intent = intent.intent;
      out.pattern = intent.pattern;
      break;
    }
  }

  std::ostringstream summary;
  summary << "object=" << out.object_id << " frame=" << out.frame
          << " shot=" << out.shot_id << " property=" << out.property
          << " source=" << track_source_id(out.source)
          << " reason=" << out.reason_code << " value=" << out.value;
  if (out.intent.has_value()) {
    summary << " intent=" << intent_kind_id(*out.intent);
  }
  if (out.pattern.has_value()) {
    summary << " pattern=" << gesture_pattern_id(*out.pattern);
  }
  out.summary = summary.str();
  return out;
}

std::string DirectionReport::to_snapshot() const {
  // Stable ordering for golden audit snapshots.
  auto intents = intents_;
  auto channels = channels_;
  auto transitions = transitions_;
  auto warnings = warnings_;
  std::sort(intents.begin(), intents.end(),
            [](const IntentDecision &a, const IntentDecision &b) {
              if (a.shot_id != b.shot_id) {
                return a.shot_id < b.shot_id;
              }
              return a.reason_code < b.reason_code;
            });
  std::sort(channels.begin(), channels.end(),
            [](const ChannelDecision &a, const ChannelDecision &b) {
              if (a.shot_id != b.shot_id) {
                return a.shot_id < b.shot_id;
              }
              if (a.object_id != b.object_id) {
                return a.object_id < b.object_id;
              }
              if (a.property != b.property) {
                return a.property < b.property;
              }
              return a.track_id < b.track_id;
            });
  std::sort(transitions.begin(), transitions.end(),
            [](const TransitionDecision &a, const TransitionDecision &b) {
              return a.id < b.id;
            });
  std::sort(warnings.begin(), warnings.end(),
            [](const DirectionWarning &a, const DirectionWarning &b) {
              if (a.code != b.code) {
                return a.code < b.code;
              }
              if (a.shot_id != b.shot_id) {
                return a.shot_id < b.shot_id;
              }
              return a.object_id < b.object_id;
            });

  std::ostringstream out;
  out << "direction_report/1\n";
  for (const IntentDecision &intent : intents) {
    out << "intent shot=";
    append_escaped(out, intent.shot_id);
    out << " kind=" << intent_kind_id(intent.intent)
        << " pattern=" << gesture_pattern_id(intent.pattern) << " package=";
    append_escaped(out, intent.package_id);
    out << " source=" << track_source_id(intent.source)
        << " intensity=" << intent.intensity
        << " importance=" << intent.importance << " reason=";
    append_escaped(out, intent.reason_code);
    out << '\n';
  }
  for (const ChannelDecision &channel : channels) {
    out << "channel shot=";
    append_escaped(out, channel.shot_id);
    out << " object=";
    append_escaped(out, channel.object_id);
    out << " property=";
    append_escaped(out, channel.property);
    out << " track=";
    append_escaped(out, channel.track_id);
    out << " source=" << track_source_id(channel.source) << " reason=";
    append_escaped(out, channel.reason_code);
    out << " keyframes=";
    for (std::size_t i = 0; i < channel.keyframes.size(); ++i) {
      if (i) {
        out << ';';
      }
      out << channel.keyframes[i].frame << ':' << channel.keyframes[i].value;
    }
    out << '\n';
  }
  for (const TransitionDecision &tr : transitions) {
    out << "transition id=";
    append_escaped(out, tr.id);
    out << " from=";
    append_escaped(out, tr.from_shot_id);
    out << " to=";
    append_escaped(out, tr.to_shot_id);
    out << " kind=" << transition_kind_id(tr.kind) << " blend=";
    append_escaped(out, tr.blend);
    out << " start=" << tr.start_frame << " duration=" << tr.duration_frames
        << " reason=";
    append_escaped(out, tr.reason_code);
    out << '\n';
  }
  for (const DirectionWarning &warning : warnings) {
    out << "warning severity="
        << direction_decision_severity_id(warning.severity) << " code=";
    append_escaped(out, warning.code);
    out << " shot=";
    append_escaped(out, warning.shot_id);
    out << " object=";
    append_escaped(out, warning.object_id);
    out << " message=";
    append_escaped(out, warning.message);
    out << '\n';
  }
  return out.str();
}

IntentDecision make_intent_decision(std::string_view shot_id,
                                    const SemanticIntent &intent,
                                    const MotionGrammarBinding &binding,
                                    TrackSource source) {
  IntentDecision decision;
  decision.shot_id = std::string(shot_id);
  decision.intent = intent.kind;
  decision.pattern = binding.pattern;
  decision.package_id = binding.package_id;
  decision.source = source;
  decision.intensity = intent.intensity;
  decision.importance = intent.importance;
  decision.reason_code = "motion_grammar_binding";
  return decision;
}

std::vector<ChannelDecision>
make_channel_decisions(std::string_view shot_id,
                       std::span<const TrackClaim> winning_claims) {
  std::vector<ChannelDecision> out;
  out.reserve(winning_claims.size());
  for (const TrackClaim &claim : winning_claims) {
    ChannelDecision decision;
    decision.shot_id = std::string(shot_id);
    decision.object_id = claim.track.target_id;
    decision.property = claim.track.property;
    decision.track_id = claim.track.id;
    decision.source = claim.source;
    decision.reason_code = reason_for_source(claim.source);
    decision.keyframes = claim.track.keyframes;
    out.push_back(std::move(decision));
  }
  return out;
}

TransitionDecision make_transition_decision(const PlannedTransition &planned) {
  TransitionDecision decision;
  decision.id = planned.id;
  decision.from_shot_id = planned.from_shot_id;
  decision.to_shot_id = planned.to_shot_id;
  decision.kind = planned.kind;
  decision.blend = planned.blend;
  decision.start_frame = planned.start_frame;
  decision.duration_frames = planned.duration_frames;
  decision.reason_code = "transition_planner";
  return decision;
}

} // namespace instantee::direction
