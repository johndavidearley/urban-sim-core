#include "src/cli/CliApp.hpp"

#include <iostream>
#include <optional>
#include <utility>

#include "src/cli/CliCityWorkflow.hpp"
#include "src/cli/CliEarlyDispatch.hpp"

int runCliApp(CliOptions opts) {
  try {
    if (const std::optional<int> early = tryEarlyCliDispatch(opts)) {
      return *early;
    }
    return runCityCliWorkflow(std::move(opts));
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
