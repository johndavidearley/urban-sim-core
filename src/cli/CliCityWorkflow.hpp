#pragma once

#include "src/cli/CliOptions.hpp"

// Build a city from options and run inspection / growth / traffic / economy /
// district / render commands (and the default empty tick loop).
int runCityCliWorkflow(CliOptions opts);
