#include "src/cli/CommutePolicySweep.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "src/cli/CliUtils.hpp"
#include "src/cli/Simulate.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/systems/CitySimulator.hpp"

namespace {

struct CommuteSummary {
  double meanCongestion = 0.0;
  double meanModalShare = 0.0;
  double meanUnmetTransitDemand = 0.0;  // mean(transitDemand - transitRidership): riders a route reached but was too full to carry
};

CommuteSummary summarizeRows(const std::vector<SimTickMetrics>& rows) {
  CommuteSummary summary;
  if (rows.empty()) {
    return summary;
  }
  double congestionSum = 0.0;
  double modalShareSum = 0.0;
  double unmetSum = 0.0;
  for (const SimTickMetrics& row : rows) {
    congestionSum += row.trafficCongestion;
    modalShareSum += row.transitModalShare;
    unmetSum += (row.transitDemand > row.transitRidership)
      ? static_cast<double>(row.transitDemand - row.transitRidership) : 0.0;
  }
  const double n = static_cast<double>(rows.size());
  summary.meanCongestion = congestionSum / n;
  summary.meanModalShare = modalShareSum / n;
  summary.meanUnmetTransitDemand = unmetSum / n;
  return summary;
}

struct SweepScenario {
  std::string label;
  std::string path;
  uint32_t seed = 0;
  float transitCapacityMultiplier = 1.0f;
  bool transitEnabled = true;
  CommuteSummary summary;
};

} // namespace

int runCommutePolicySweep(
  CommutePolicySweepOptions options,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population
) {
  if (options.ticks <= 0) {
    std::cerr << "Error: --commute-sweep requires --commute-sweep-ticks N (N > 0)\n";
    return 1;
  }
  if (options.seeds.empty()) {
    std::cerr << "Error: --commute-sweep requires --commute-sweep-seeds A,B,C\n";
    return 1;
  }
  if (options.transitCapacityMultipliers.empty()) {
    options.transitCapacityMultipliers.push_back(1.0f);
  }

  std::error_code fsError;
  std::filesystem::create_directories(options.outputDir, fsError);
  if (fsError) {
    std::cerr << "Error: Failed to create sweep output directory '" << options.outputDir
              << "': " << fsError.message() << "\n";
    return 1;
  }

  const int mapSize = map.getDimensions().x;
  const CitySnapshot baselineSnapshot = SaveLoadSystem::captureSnapshot(map, roads, store, population);
  const uint32_t baselineSeed = options.seeds.front();
  constexpr float kBaselineMultiplier = 1.0f;

  auto runScenario = [&](uint32_t seed, float multiplier, bool transitEnabled,
                         const std::string& label, const std::string& path) -> SweepScenario {
    CityMap scenarioMap({mapSize, mapSize});
    RoadNetwork scenarioRoads(scenarioMap);
    EntityStore scenarioStore;
    PopulationStore scenarioPopulation;

    if (!SaveLoadSystem::applySnapshot(baselineSnapshot, scenarioMap, scenarioRoads, scenarioStore, scenarioPopulation)) {
      throw std::runtime_error("failed to apply baseline snapshot");
    }

    SimOptions simOptions;
    simOptions.enableTransit = transitEnabled;
    simOptions.transitCapacityMultiplier = multiplier;

    const SimResult result = CitySimulator::run(
      scenarioMap, scenarioRoads, scenarioStore, scenarioPopulation, seed, options.ticks, simOptions);

    if (!writeSimulationReportCSV(path, result.rows)) {
      throw std::runtime_error("failed to write per-tick report");
    }

    SweepScenario scenario;
    scenario.label = label;
    scenario.path = path;
    scenario.seed = seed;
    scenario.transitCapacityMultiplier = multiplier;
    scenario.transitEnabled = transitEnabled;
    scenario.summary = summarizeRows(result.rows);
    return scenario;
  };

  std::cout << "Running commute policy sweep in " << options.outputDir << "\n";
  std::cout << "  Baseline: seed=" << baselineSeed << " transitCapacityMultiplier=" << kBaselineMultiplier
            << " transitEnabled=yes\n";

  const std::string baselinePath = (std::filesystem::path(options.outputDir) / "baseline.csv").string();
  SweepScenario baseline;
  try {
    baseline = runScenario(baselineSeed, kBaselineMultiplier, true, "baseline", baselinePath);
  } catch (const std::exception& ex) {
    std::cerr << "Error: Commute policy sweep baseline failed: " << ex.what() << "\n";
    return 1;
  }
  std::cout << "  Wrote baseline report: " << baselinePath << "\n";

  std::vector<SweepScenario> candidates;
  size_t ordinal = 0;
  for (uint32_t seed : options.seeds) {
    for (float multiplier : options.transitCapacityMultipliers) {
      if (seed == baselineSeed && std::abs(multiplier - kBaselineMultiplier) < 0.0001f) {
        continue;  // already ran as the baseline
      }
      ++ordinal;
      std::ostringstream fileName;
      fileName << "scenario_" << ordinal << "_seed" << seed
               << "_transit" << sanitizeToken(std::to_string(multiplier)) << "x.csv";
      const std::string path = (std::filesystem::path(options.outputDir) / fileName.str()).string();
      try {
        SweepScenario scenario = runScenario(seed, multiplier, true, fileName.str(), path);
        std::cout << "  Wrote scenario report: " << path << "\n";
        candidates.push_back(std::move(scenario));
      } catch (const std::exception& ex) {
        std::cerr << "Error: Commute policy sweep scenario failed for seed=" << seed
                  << " transitCapacityMultiplier=" << multiplier << ": " << ex.what() << "\n";
        return 1;
      }
    }

    if (options.includeTransitDisabledScenario) {
      ++ordinal;
      std::ostringstream fileName;
      fileName << "scenario_" << ordinal << "_seed" << seed << "_transitDisabled.csv";
      const std::string path = (std::filesystem::path(options.outputDir) / fileName.str()).string();
      try {
        SweepScenario scenario = runScenario(seed, 1.0f, false, fileName.str(), path);
        std::cout << "  Wrote scenario report: " << path << "\n";
        candidates.push_back(std::move(scenario));
      } catch (const std::exception& ex) {
        std::cerr << "Error: Commute policy sweep scenario failed for seed=" << seed
                  << " (transit disabled): " << ex.what() << "\n";
        return 1;
      }
    }
  }

  const std::string manifestPath = (std::filesystem::path(options.outputDir) / "sweep_manifest.csv").string();
  std::ofstream manifest(manifestPath);
  if (!manifest.is_open()) {
    std::cerr << "Error: Failed to write sweep manifest to '" << manifestPath << "'\n";
    return 1;
  }

  manifest << "scenario,seed,transit_enabled,transit_capacity_multiplier,report_path,"
              "mean_congestion,mean_modal_share,mean_unmet_transit_demand,"
              "d_congestion,d_modal_share,d_unmet_transit_demand\n";
  manifest << "baseline," << baseline.seed << ",1," << kBaselineMultiplier << ","
           << csvEscape(baseline.path) << ","
           << std::fixed << std::setprecision(6)
           << baseline.summary.meanCongestion << "," << baseline.summary.meanModalShare << ","
           << baseline.summary.meanUnmetTransitDemand << ",0.000000,0.000000,0.000000\n";

  for (const SweepScenario& scenario : candidates) {
    manifest << csvEscape(scenario.label) << "," << scenario.seed << ","
             << (scenario.transitEnabled ? 1 : 0) << "," << scenario.transitCapacityMultiplier << ","
             << csvEscape(scenario.path) << ","
             << std::fixed << std::setprecision(6)
             << scenario.summary.meanCongestion << "," << scenario.summary.meanModalShare << ","
             << scenario.summary.meanUnmetTransitDemand << ","
             << (scenario.summary.meanCongestion - baseline.summary.meanCongestion) << ","
             << (scenario.summary.meanModalShare - baseline.summary.meanModalShare) << ","
             << (scenario.summary.meanUnmetTransitDemand - baseline.summary.meanUnmetTransitDemand) << "\n";
  }
  std::cout << "Wrote sweep manifest: " << manifestPath << "\n";

  std::vector<const SweepScenario*> ranked;
  ranked.reserve(candidates.size());
  for (const SweepScenario& scenario : candidates) {
    ranked.push_back(&scenario);
  }
  std::sort(ranked.begin(), ranked.end(), [](const SweepScenario* a, const SweepScenario* b) {
    return a->summary.meanCongestion < b->summary.meanCongestion;
  });

  std::cout << "\nCommute Policy Ranking (by mean congestion, lowest first; baseline="
            << std::fixed << std::setprecision(3) << baseline.summary.meanCongestion << "):\n";
  for (const SweepScenario* scenario : ranked) {
    std::cout << "  " << scenario->label
              << ": seed=" << scenario->seed
              << " transit=" << (scenario->transitEnabled ? "on" : "off")
              << " multiplier=" << std::setprecision(2) << scenario->transitCapacityMultiplier
              << " congestion=" << std::setprecision(3) << scenario->summary.meanCongestion
              << " (d=" << (scenario->summary.meanCongestion - baseline.summary.meanCongestion) << ")"
              << " modalShare=" << scenario->summary.meanModalShare
              << " unmetDemand=" << scenario->summary.meanUnmetTransitDemand << "\n";
  }

  return 0;
}
