#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/export_core/output_transaction.hpp>

#include <expected>
#include <filesystem>
#include <string>

namespace instantee::export_core {

// Filesystem facts that gate DurabilityClass selection. Probe before starting a
// durable-local job; never silently demote a stronger requested class.
struct DurabilityCapability {
  std::string filesystem_type;
  bool process_crash_visible = false;
  bool durable_local = false;
  // Process SIGKILL proves crash-visible atomicity only. It is never evidence
  // of power-loss or media durability.
  bool sigkill_is_power_loss_evidence = false;
};

// Inspect the concrete mount that owns `parent_directory`. Empty or missing
// parents fail; exotic mounts keep process_crash_visible but refuse
// durable_local.
[[nodiscard]] std::expected<DurabilityCapability, diagnostics::Error>
probe_durability_capability(const std::filesystem::path &parent_directory);

// Rejects durable_local when the mount cannot honour F_FULLFSYNC semantics.
[[nodiscard]] std::expected<void, diagnostics::Error>
require_durability_support(DurabilityClass requested,
                           const DurabilityCapability &capability);

} // namespace instantee::export_core
