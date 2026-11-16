#include <instantee/diagnostics/error.hpp>
#include <instantee/diagnostics/exception_boundary.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using instantee::diagnostics::Error;
using instantee::diagnostics::ErrorKind;
using instantee::diagnostics::ExitCode;
using instantee::diagnostics::map_current_exception;
using instantee::diagnostics::map_exception;

[[nodiscard]] int check_taxonomy_and_ownership() {
  Error base{ExitCode::invalid_input, "schema_invalid", "validate",
             "Node type mismatch"};
  Error with_path = base.with_json_path("/shots/0/nodes/2");
  if (with_path.context.json_path != "/shots/0/nodes/2" ||
      with_path.message != "Node type mismatch" ||
      with_path.code != "schema_invalid") {
    return 1;
  }

  // Lifetime: destroy the source path string; Error must keep its own copy.
  {
    std::string ephemeral = "/films/demo/shots/intro";
    base =
        Error{ExitCode::invalid_input, "id_collision", "ids", "Duplicate shot"}
            .with_json_path(ephemeral)
            .with_shot("intro")
            .with_frame(42)
            .with_node("title");
    ephemeral.assign(ephemeral.size(), 'x');
  }
  if (!base.context.json_path ||
      *base.context.json_path != "/films/demo/shots/intro" ||
      !base.context.shot || *base.context.shot != "intro" ||
      !base.context.frame || *base.context.frame != 42 || !base.context.node ||
      *base.context.node != "title") {
    return 2;
  }
  return 0;
}

[[nodiscard]] int check_distinct_kinds() {
  const Error cancelled{ExitCode::invalid_input, "cancelled", "bounded_queue",
                        "Queue aborted"};
  const Error closed{ExitCode::invalid_input, "closed", "bounded_queue",
                     "Queue closed"};
  const Error blocked{ExitCode::invalid_input, "would_block", "bounded_queue",
                      "Queue full"};
  const Error durability{ExitCode::published_durability_unknown,
                         "published_durability_unknown", "publish",
                         "Durability unknown after publish"};
  if (cancelled.kind != ErrorKind::cancelled ||
      cancelled.exit_code != ExitCode::cancelled ||
      closed.kind != ErrorKind::closed ||
      closed.exit_code != ExitCode::closed ||
      blocked.kind != ErrorKind::would_block ||
      blocked.exit_code != ExitCode::would_block ||
      durability.kind != ErrorKind::published_durability_unknown) {
    return 3;
  }
  return 0;
}

[[nodiscard]] int check_snapshots_stable() {
  const Error error = Error{ExitCode::invalid_input, ErrorKind::invalid_input,
                            "id_collision", "ids", "Duplicate node identifier"}
                          .with_json_path("/nodes/3/id")
                          .with_node("hero");
  const std::string first = error.snapshot();
  const std::string second = error.snapshot();
  constexpr std::string_view expected =
      "kind=1 code=id_collision phase=ids message=Duplicate node identifier "
      "exit=2 json_path=/nodes/3/id node=hero";
  if (first != second || first != expected) {
    return 4;
  }
  return 0;
}

[[nodiscard]] int check_exception_boundary() {
  try {
    throw std::runtime_error("boom");
  } catch (...) {
    const Error mapped = map_current_exception("cli");
    if (mapped.kind != ErrorKind::unexpected_exception ||
        mapped.exit_code != ExitCode::unexpected_exception ||
        mapped.phase != "cli" || mapped.message != "boom") {
      return 5;
    }
  }

  try {
    throw std::bad_alloc();
  } catch (const std::exception &ex) {
    const Error mapped = map_exception(ex, "worker");
    if (mapped.kind != ErrorKind::out_of_memory ||
        mapped.exit_code != ExitCode::out_of_memory ||
        mapped.code != "out_of_memory") {
      return 6;
    }
  }
  return 0;
}

} // namespace

int main() {
  if (const int code = check_taxonomy_and_ownership(); code != 0) {
    return code;
  }
  if (const int code = check_distinct_kinds(); code != 0) {
    return code;
  }
  if (const int code = check_snapshots_stable(); code != 0) {
    return code;
  }
  return check_exception_boundary();
}
