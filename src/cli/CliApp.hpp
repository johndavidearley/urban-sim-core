#pragma once

#include "src/cli/CliOptions.hpp"

// Execute the CLI workflow for the parsed options. Returns process exit code.
int runCliApp(CliOptions opts);
