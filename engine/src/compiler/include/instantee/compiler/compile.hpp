#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/direction_report.hpp>
#include <instantee/scene_ir/authoring.hpp>
#include <instantee/scene_ir/document.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace instantee::compiler {

struct CompileOptions {
  std::string compiler_version = "0.1.0";
  std::string target_feature_profile = "metal-apple-baseline";
};

// Wall times in milliseconds for the named stages. Zero when a stage is
// skipped.
struct CompileStageTiming {
  double parse_ms = 0;
  double structural_ms = 0;
  double compile_ms = 0;
  double graph_ms = 0;
  double canonicalize_ms = 0;
  double write_ms = 0;
  std::uint64_t ir_bytes = 0;
};

struct CompileAuthoringResult {
  scene_ir::SceneDocument document;
  direction::DirectionReport direction_report;
};

// CDS compile: validate the Style Package, lower shot intents into concrete
// animation tracks, and emit Scene IR with no unresolved semantic intent.
// Unsupported or unimplemented direction fails closed (never identity-dropped).
// When `direction_report` is non-null, an auditable CDS decision log is filled.
[[nodiscard]] std::expected<scene_ir::SceneDocument, diagnostics::Error>
compile_authoring(const scene_ir::AuthoringDocument &authoring,
                  const CompileOptions &options,
                  direction::DirectionReport *direction_report = nullptr);

[[nodiscard]] std::expected<CompileAuthoringResult, diagnostics::Error>
compile_authoring_with_report(const scene_ir::AuthoringDocument &authoring,
                              const CompileOptions &options);

[[nodiscard]] std::expected<scene_ir::SceneDocument, diagnostics::Error>
compile_authoring_json(std::span<const std::byte> bytes,
                       const CompileOptions &options,
                       CompileStageTiming *timing = nullptr,
                       direction::DirectionReport *direction_report = nullptr);

[[nodiscard]] std::expected<std::string, diagnostics::Error>
scene_document_to_json(const scene_ir::SceneDocument &document);

} // namespace instantee::compiler
