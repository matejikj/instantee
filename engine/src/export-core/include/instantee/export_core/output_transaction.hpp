#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace instantee::export_core {

// Default publish mode refuses to replace an existing target. Overwrite is an
// explicit atomic replacement; the old target is never truncated beforehand.
enum class PublishMode : std::uint8_t { no_replace, overwrite };

// process_crash_visible uses fsync for atomic visibility after process death.
// durable_local requires F_FULLFSYNC on Darwin and refuses unsupported volumes
// instead of silently falling back.
enum class DurabilityClass : std::uint8_t {
  process_crash_visible,
  durable_local
};

enum class DurabilityState : std::uint8_t {
  not_applicable,
  confirmed,
  unknown
};

// Test-only fault points. Production configs leave every flag false.
struct OutputTransactionFaults {
  bool fail_file_sync_before_publish = false;
  bool fail_directory_sync_after_publish = false;
};

struct OutputTransactionConfig {
  std::filesystem::path target_path;
  PublishMode mode = PublishMode::no_replace;
  DurabilityClass durability = DurabilityClass::durable_local;
  OutputTransactionFaults faults{};
};

// Outcome of an explicit commit. published=true means the rename linearization
// point succeeded; durability may still be unknown when metadata sync fails.
struct PublishResult {
  bool published = false;
  DurabilityState durability = DurabilityState::not_applicable;
  // Set when the caller must exit nonzero even though the MP4 is visible.
  std::optional<spike::Error> post_publish_error;
};

// Owns a private staging directory beside the target. Destructor only cleans
// unpublished staging; it never renames and never deletes a published target.
class OutputTransaction {
public:
  OutputTransaction(OutputTransaction &&) noexcept;
  OutputTransaction &operator=(OutputTransaction &&) noexcept;
  ~OutputTransaction();

  OutputTransaction(const OutputTransaction &) = delete;
  OutputTransaction &operator=(const OutputTransaction &) = delete;

  [[nodiscard]] static std::expected<OutputTransaction, spike::Error>
  begin(OutputTransactionConfig config);

  [[nodiscard]] const std::filesystem::path &target_path() const noexcept;
  [[nodiscard]] const std::filesystem::path &staging_directory() const noexcept;
  // Non-existent file path inside the staging directory for AVAssetWriter.
  [[nodiscard]] const std::filesystem::path &staging_file() const noexcept;
  [[nodiscard]] bool published() const noexcept;
  [[nodiscard]] const std::string &job_nonce() const noexcept;

  // Requires the staging file to already contain the finished payload. Syncs
  // the file, publishes with exclusive or replacing rename, then syncs parent
  // metadata. Pre-publish failures leave the old target unchanged.
  [[nodiscard]] std::expected<PublishResult, spike::Error> commit();

private:
  struct State;
  explicit OutputTransaction(std::unique_ptr<State> state) noexcept;

  std::unique_ptr<State> state_{};
};

} // namespace instantee::export_core
