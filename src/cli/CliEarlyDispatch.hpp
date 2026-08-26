#pragma once

#include <optional>

#include "src/cli/CliOptions.hpp"

// Handles CLI modes that exit before a full interactive city session is built
// (benchmarks, simulate, micro-traffic, snapshot inspect/audit, pressure tools).
// Returns an exit code when handled; nullopt means continue to the city workflow.
std::optional<int> tryEarlyCliDispatch(CliOptions& opts);
