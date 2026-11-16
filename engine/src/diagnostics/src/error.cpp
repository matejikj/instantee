#include <instantee/diagnostics/error.hpp>

#include <sstream>
#include <utility>

namespace instantee::diagnostics {
namespace {

[[nodiscard]] ErrorKind infer_kind(ExitCode exit_code,
                                   std::string_view code) noexcept {
  const ErrorKind from_code = Error::kind_for_code(code);
  if (from_code != ErrorKind::invalid_input) {
    return from_code;
  }
  return Error::kind_for(exit_code);
}

} // namespace

ErrorKind Error::kind_for(ExitCode exit_code) noexcept {
  switch (exit_code) {
  case ExitCode::invalid_input:
    return ErrorKind::invalid_input;
  case ExitCode::gpu_failure:
    return ErrorKind::gpu_failure;
  case ExitCode::export_failure:
    return ErrorKind::export_failure;
  case ExitCode::publication_conflict:
    return ErrorKind::publication_conflict;
  case ExitCode::published_durability_unknown:
    return ErrorKind::published_durability_unknown;
  case ExitCode::report_failure:
    return ErrorKind::report_failure;
  case ExitCode::deadline_exceeded:
    return ErrorKind::deadline_exceeded;
  case ExitCode::cancelled:
    return ErrorKind::cancelled;
  case ExitCode::closed:
    return ErrorKind::closed;
  case ExitCode::would_block:
    return ErrorKind::would_block;
  case ExitCode::resource_budget_exceeded:
    return ErrorKind::resource_budget_exceeded;
  case ExitCode::out_of_memory:
    return ErrorKind::out_of_memory;
  case ExitCode::unexpected_exception:
    return ErrorKind::unexpected_exception;
  }
  return ErrorKind::invalid_input;
}

ExitCode Error::exit_code_for(ErrorKind kind) noexcept {
  switch (kind) {
  case ErrorKind::invalid_input:
    return ExitCode::invalid_input;
  case ErrorKind::cancelled:
    return ExitCode::cancelled;
  case ErrorKind::closed:
    return ExitCode::closed;
  case ErrorKind::would_block:
    return ExitCode::would_block;
  case ErrorKind::deadline_exceeded:
    return ExitCode::deadline_exceeded;
  case ErrorKind::resource_budget_exceeded:
    return ExitCode::resource_budget_exceeded;
  case ErrorKind::gpu_failure:
    return ExitCode::gpu_failure;
  case ErrorKind::export_failure:
    return ExitCode::export_failure;
  case ErrorKind::publication_conflict:
    return ExitCode::publication_conflict;
  case ErrorKind::published_durability_unknown:
    return ExitCode::published_durability_unknown;
  case ErrorKind::report_failure:
    return ExitCode::report_failure;
  case ErrorKind::out_of_memory:
    return ExitCode::out_of_memory;
  case ErrorKind::unexpected_exception:
    return ExitCode::unexpected_exception;
  }
  return ExitCode::invalid_input;
}

ErrorKind Error::kind_for_code(std::string_view code) noexcept {
  if (code == "cancelled") {
    return ErrorKind::cancelled;
  }
  if (code == "closed") {
    return ErrorKind::closed;
  }
  if (code == "would_block" || code == "blocked") {
    return ErrorKind::would_block;
  }
  if (code == "deadline_exceeded") {
    return ErrorKind::deadline_exceeded;
  }
  if (code == "resource_budget_exceeded") {
    return ErrorKind::resource_budget_exceeded;
  }
  if (code == "published_durability_unknown") {
    return ErrorKind::published_durability_unknown;
  }
  if (code == "publication_conflict") {
    return ErrorKind::publication_conflict;
  }
  if (code == "out_of_memory") {
    return ErrorKind::out_of_memory;
  }
  if (code == "unexpected_exception") {
    return ErrorKind::unexpected_exception;
  }
  if (code == "gpu_failure" || code == "renderer_unavailable") {
    return ErrorKind::gpu_failure;
  }
  if (code == "report_failure" || code == "stdout_failed") {
    return ErrorKind::report_failure;
  }
  return ErrorKind::invalid_input;
}

Error::Error(ExitCode exit_code_in, std::string_view code_in,
             std::string_view phase_in, std::string_view message_in)
    : exit_code(exit_code_in), kind(infer_kind(exit_code_in, code_in)),
      code(code_in), phase(phase_in), message(message_in) {
  // Prefer taxonomy-derived exit codes for cancelled/closed/blocked so callers
  // that still pass ExitCode::invalid_input with those codes stay typed.
  if (kind == ErrorKind::cancelled || kind == ErrorKind::closed ||
      kind == ErrorKind::would_block ||
      kind == ErrorKind::resource_budget_exceeded ||
      kind == ErrorKind::out_of_memory ||
      kind == ErrorKind::unexpected_exception) {
    exit_code = exit_code_for(kind);
  }
}

Error::Error(ExitCode exit_code_in, ErrorKind kind_in, std::string_view code_in,
             std::string_view phase_in, std::string_view message_in)
    : exit_code(exit_code_in), kind(kind_in), code(code_in), phase(phase_in),
      message(message_in) {}

Error::Error(ExitCode exit_code_in, ErrorKind kind_in, std::string_view code_in,
             std::string_view phase_in, std::string_view message_in,
             ErrorContext context_in)
    : exit_code(exit_code_in), kind(kind_in), code(code_in), phase(phase_in),
      message(message_in), context(std::move(context_in)) {}

Error Error::with_json_path(std::string path) const & {
  Error copy = *this;
  copy.context.json_path = std::move(path);
  return copy;
}

Error Error::with_json_path(std::string path) && {
  context.json_path = std::move(path);
  return std::move(*this);
}

Error Error::with_frame(std::uint64_t frame) const & {
  Error copy = *this;
  copy.context.frame = frame;
  return copy;
}

Error Error::with_frame(std::uint64_t frame) && {
  context.frame = frame;
  return std::move(*this);
}

Error Error::with_shot(std::string shot) const & {
  Error copy = *this;
  copy.context.shot = std::move(shot);
  return copy;
}

Error Error::with_shot(std::string shot) && {
  context.shot = std::move(shot);
  return std::move(*this);
}

Error Error::with_node(std::string node) const & {
  Error copy = *this;
  copy.context.node = std::move(node);
  return copy;
}

Error Error::with_node(std::string node) && {
  context.node = std::move(node);
  return std::move(*this);
}

Error Error::with_system_code(int system_code) const & {
  Error copy = *this;
  copy.context.system_code = system_code;
  return copy;
}

Error Error::with_system_code(int system_code) && {
  context.system_code = system_code;
  return std::move(*this);
}

std::string Error::snapshot() const {
  std::ostringstream out;
  out << "kind=" << static_cast<unsigned>(kind) << " code=" << code
      << " phase=" << phase << " message=" << message
      << " exit=" << static_cast<int>(exit_code);
  if (context.json_path) {
    out << " json_path=" << *context.json_path;
  }
  if (context.frame) {
    out << " frame=" << *context.frame;
  }
  if (context.shot) {
    out << " shot=" << *context.shot;
  }
  if (context.node) {
    out << " node=" << *context.node;
  }
  if (context.system_code) {
    out << " system_code=" << *context.system_code;
  }
  return out.str();
}

} // namespace instantee::diagnostics
