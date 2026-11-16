#pragma once

#include <instantee/diagnostics/error.hpp>

#include <expected>

namespace instantee::spike::apple {

// Synchronous adapter smoke check; owns no persistent state and performs no
// I/O. Video resources and their asynchronous lifetimes are not initialized
// here.
[[nodiscard]] std::expected<void, Error> check_workspace();

} // namespace instantee::spike::apple
