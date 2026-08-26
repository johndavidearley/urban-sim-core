#pragma once

#include <optional>

#include "src/cli/CliOptions.hpp"

// Parse argv into opts and validate cross-flag constraints.
// Returns nullopt to continue into runCliApp, or an exit code for main.
std::optional<int> parseCliArgs(int argc, char* argv[], CliOptions& opts);
