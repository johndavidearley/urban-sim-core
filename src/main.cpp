#include "src/cli/CliApp.hpp"
#include "src/cli/CliOptions.hpp"
#include "src/cli/CliParse.hpp"

int main(int argc, char* argv[]) {
  CliOptions opts;
  if (const std::optional<int> early = parseCliArgs(argc, argv, opts)) {
    return *early;
  }
  return runCliApp(opts);
}
