#pragma once

#include <cstdint>

#include "src/cli/CliUtils.hpp"

// Runs the Phase 5 performance benchmark on a synthetic city and prints the
// per-phase timing breakdown. Returns the process exit code.
int runPhase5Benchmark(int mapSize, uint32_t seed, int benchmarkTicks, BenchmarkFocus focus);
