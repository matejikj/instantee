#pragma once

#include <cstdint>
#include <string_view>

namespace instantee::core {

// Deterministic mixing helpers for FrameState(n) procedural channels.
// Contract (SAS §7.1): sample = f(seed, node_id, property_id, frame). No
// global RNG state, no std::hash, no unordered_map iteration order.

// SplitMix64 finalizer / steppable mixer (explicit algorithm, fixed constants).
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t value) noexcept {
  std::uint64_t z = value + 0x9E3779B97F4A7C15ULL;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

// FNV-1a 64-bit over raw bytes (stable across platforms and standard
// libraries).
[[nodiscard]] constexpr std::uint64_t fnv1a64(std::string_view bytes) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char ch : bytes) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= 1099511628211ULL;
  }
  return hash;
}

// Channel key: hash(seed, node_id, property_id, frame). Independent of any
// other node or of evaluation order.
[[nodiscard]] constexpr std::uint64_t
hash_animation_channel(std::uint64_t seed, std::string_view node_id,
                       std::string_view property_id,
                       std::uint64_t frame) noexcept {
  std::uint64_t h = splitmix64(seed);
  h = splitmix64(h ^ fnv1a64(node_id));
  h = splitmix64(h ^ fnv1a64(property_id));
  h = splitmix64(h ^ frame);
  return h;
}

// Map 64-bit entropy to unit interval [0, 1) using the top 53 bits.
[[nodiscard]] inline double hash_to_unit(std::uint64_t hash) noexcept {
  constexpr double kScale = 1.0 / static_cast<double>(1ULL << 53U);
  return static_cast<double>(hash >> 11U) * kScale;
}

[[nodiscard]] inline double sample_unit_noise(std::uint64_t seed,
                                              std::string_view node_id,
                                              std::string_view property_id,
                                              std::uint64_t frame) noexcept {
  return hash_to_unit(
      hash_animation_channel(seed, node_id, property_id, frame));
}

} // namespace instantee::core
