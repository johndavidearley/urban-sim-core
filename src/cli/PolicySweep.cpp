#include "src/cli/PolicySweep.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "src/cli/CliUtils.hpp"
#include "src/cli/GrowthPressureReport.hpp"
#include "src/persistence/SaveLoadSystem.hpp"

int runPolicySweep(
  PolicySweepOptions options,
  DistrictSystem& districtSystem,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population,
  const std::vector<ServiceFacility>& serviceFacilities
) {
  const District* baselineDistrict = districtSystem.getDistrictConst(static_cast<DistrictId>(options.districtId));
  if (baselineDistrict == nullptr) {
    std::cerr << "Error: Sweep district " << options.districtId << " not found\n";
    return 1;
  }

  const int64_t baselineCap = baselineDistrict->serviceBudgetCap;
  const float baselineAllocation = baselineDistrict->serviceAllocation;
  const int mapSize = map.getDimensions().x;

  if (options.caps.empty()) {
    options.caps.push_back(baselineCap);
  }
  if (options.allocations.empty()) {
    options.allocations.push_back(baselineAllocation);
  }

  for (float& allocation : options.allocations) {
    allocation = std::max(0.0f, std::min(1.0f, allocation));
  }

  std::error_code fsError;
  std::filesystem::create_directories(options.outputDir, fsError);
  if (fsError) {
    std::cerr << "Error: Failed to create sweep output directory '" << options.outputDir
              << "': " << fsError.message() << "\n";
    return 1;
  }

  const CitySnapshot baselineSnapshot = SaveLoadSystem::captureSnapshot(map, roads, store, population);

  struct SweepResult {
    std::string label;
    std::string path;
    uint32_t scenarioSeed = 0;
    int64_t cap = -1;
    float allocation = 0.0f;
    std::vector<GrowthPressureReportRow> rows;
  };

  auto runSweepScenario = [&](uint32_t scenarioSeed, int64_t cap, float allocation, const std::string& label, const std::string& path) -> SweepResult {
    CityMap scenarioMap({mapSize, mapSize});
    RoadNetwork scenarioRoads(scenarioMap);
    EntityStore scenarioStore;
    PopulationStore scenarioPopulation;

    if (!SaveLoadSystem::applySnapshot(baselineSnapshot, scenarioMap, scenarioRoads, scenarioStore, scenarioPopulation)) {
      throw std::runtime_error("failed to apply baseline snapshot");
    }

    if (!districtSystem.setDistrictServiceBudgetCap(static_cast<DistrictId>(options.districtId), cap)) {
      throw std::runtime_error("failed to apply sweep service cap");
    }
    if (!districtSystem.setDistrictServiceAllocation(static_cast<DistrictId>(options.districtId), allocation)) {
      throw std::runtime_error("failed to apply sweep service allocation");
    }

    SweepResult result;
    result.label = label;
    result.path = path;
    result.scenarioSeed = scenarioSeed;
    result.cap = cap;
    result.allocation = allocation;

    runGrowthStepsWithPressure(
      districtSystem,
      scenarioMap,
      scenarioRoads,
      scenarioStore,
      scenarioPopulation,
      serviceFacilities,
      options.growthSteps,
      scenarioSeed,
      options.pressurePool,
      false,
      false,
      &result.rows
    );

    if (!writeGrowthPressureReportCSV(path, result.rows)) {
      throw std::runtime_error("failed to write growth pressure report");
    }

    return result;
  };

  std::cout << "Running policy sweep in " << options.outputDir << "\n";
  std::cout << "  District: " << options.districtId << "\n";
  std::cout << "  Baseline: seed=" << options.baseSeed
            << " cap=" << baselineCap
            << " allocation=" << std::fixed << std::setprecision(3) << baselineAllocation << "\n";

  const std::string baselinePath = (std::filesystem::path(options.outputDir) / "baseline.csv").string();
  SweepResult baselineResult;
  try {
    baselineResult = runSweepScenario(options.baseSeed, baselineCap, baselineAllocation, "baseline", baselinePath);
  } catch (const std::exception& ex) {
    std::cerr << "Error: Policy sweep baseline failed: " << ex.what() << "\n";
    return 1;
  }
  std::cout << "  Wrote baseline report: " << baselinePath
            << " (rows=" << baselineResult.rows.size() << ")\n";

  std::vector<SweepResult> candidateResults;
  size_t scenarioOrdinal = 0;
  for (uint32_t scenarioSeed : options.seeds) {
    for (int64_t cap : options.caps) {
      for (float allocation : options.allocations) {
        if (scenarioSeed == options.baseSeed && cap == baselineCap && std::abs(allocation - baselineAllocation) < 0.0001f) {
          continue;
        }

        ++scenarioOrdinal;
        std::ostringstream fileName;
        fileName << "scenario_" << scenarioOrdinal
                 << "_seed" << scenarioSeed
                 << "_cap" << cap
                 << "_alloc" << sanitizeToken(std::to_string(allocation))
                 << ".csv";
        const std::string reportPath = (std::filesystem::path(options.outputDir) / fileName.str()).string();

        try {
          SweepResult result = runSweepScenario(scenarioSeed, cap, allocation, fileName.str(), reportPath);
          std::cout << "  Wrote scenario report: " << reportPath
                    << " (rows=" << result.rows.size() << ")\n";
          candidateResults.push_back(std::move(result));
        } catch (const std::exception& ex) {
          std::cerr << "Error: Policy sweep scenario failed for seed=" << scenarioSeed
                    << " cap=" << cap << " allocation=" << allocation
                    << ": " << ex.what() << "\n";
          return 1;
        }
      }
    }
  }

  std::vector<std::pair<std::string, std::vector<GrowthPressureReportRow>>> rankingCandidates;
  rankingCandidates.reserve(candidateResults.size());
  for (const SweepResult& result : candidateResults) {
    rankingCandidates.emplace_back(result.path, result.rows);
  }

  if (!rankingCandidates.empty()) {
    printGrowthPressureRanking(baselinePath, baselineResult.rows, rankingCandidates);
    printDistrictDeltaRanking(districtSystem, baselineResult.rows, rankingCandidates, 3);
  } else {
    std::cout << "Growth Pressure Ranking: no candidate scenarios generated beyond baseline\n";
  }

  const GrowthPressureSummary baselineSummary = summarizeGrowthPressureRows(baselineResult.rows);
  const GrowthPressureSummary baselineDistrictSummary = summarizeGrowthPressureRowsForDistrict(
    baselineResult.rows,
    static_cast<DistrictId>(options.districtId)
  );

  const std::string manifestPath = (std::filesystem::path(options.outputDir) / "sweep_manifest.csv").string();
  std::ofstream manifest(manifestPath);
  if (!manifest.is_open()) {
    std::cerr << "Error: Failed to write sweep manifest to '" << manifestPath << "'\n";
    return 1;
  }

  manifest << "scenario,seed,district_id,cap,allocation,report_path,score,d_multiplier,d_fulfillment,d_cap_rate,d_alloc_share,district_samples,d_district_multiplier,d_district_fulfillment,d_district_cap_rate,d_district_alloc_share\n";
  manifest << "baseline," << options.baseSeed << "," << options.districtId << "," << baselineCap << ","
           << std::fixed << std::setprecision(6) << baselineAllocation << ","
           << csvEscape(baselinePath) << ",0.000000,0.000000,0.000000,0.000000,0.000000,"
           << baselineDistrictSummary.samples
           << ",0.000000,0.000000,0.000000,0.000000\n";

  for (const SweepResult& result : candidateResults) {
    const GrowthPressureSummary candidateSummary = summarizeGrowthPressureRows(result.rows);
    const GrowthPressureSummary candidateDistrictSummary = summarizeGrowthPressureRowsForDistrict(
      result.rows,
      static_cast<DistrictId>(options.districtId)
    );

    const GrowthPressureDelta overallDelta = computeGrowthPressureDelta(baselineSummary, candidateSummary);
    const GrowthPressureDelta districtDelta = computeGrowthPressureDelta(baselineDistrictSummary, candidateDistrictSummary);
    const double score = scoreGrowthPressureSummaryDelta(baselineSummary, candidateSummary);

    manifest << csvEscape(result.label) << ","
             << result.scenarioSeed << ","
             << options.districtId << ","
             << result.cap << ","
             << std::fixed << std::setprecision(6) << result.allocation << ","
             << csvEscape(result.path) << ","
             << std::fixed << std::setprecision(6) << score << ","
             << overallDelta.dMultiplier << ","
             << overallDelta.dFulfillment << ","
             << overallDelta.dCapRate << ","
             << overallDelta.dAllocShare << ","
             << candidateDistrictSummary.samples << ","
             << districtDelta.dMultiplier << ","
             << districtDelta.dFulfillment << ","
             << districtDelta.dCapRate << ","
             << districtDelta.dAllocShare << "\n";
  }

  std::cout << "Wrote sweep manifest: " << manifestPath << "\n";

  if (options.manifestAllDistricts) {
    const std::string districtManifestPath =
      (std::filesystem::path(options.outputDir) / "sweep_manifest_districts.csv").string();
    std::ofstream districtManifest(districtManifestPath);
    if (!districtManifest.is_open()) {
      std::cerr << "Error: Failed to write district sweep manifest to '" << districtManifestPath << "'\n";
      return 1;
    }

    districtManifest << "scenario,seed,district_id,district_name,cap,allocation,report_path,score,district_samples,d_district_multiplier,d_district_fulfillment,d_district_cap_rate,d_district_alloc_share\n";

    std::unordered_map<DistrictId, std::string> districtNames;
    for (const District& district : districtSystem.getDistricts()) {
      districtNames[district.id] = district.name;
    }

    const auto baselineByDistrict = summarizeGrowthPressureRowsByDistrict(baselineResult.rows);
    for (const auto& [districtId, summary] : baselineByDistrict) {
      std::string districtName = std::to_string(districtId);
      auto nameIt = districtNames.find(districtId);
      if (nameIt != districtNames.end()) {
        districtName = nameIt->second;
      }

      districtManifest << "baseline," << options.baseSeed << "," << districtId << ","
                       << csvEscape(districtName) << ","
                       << baselineCap << ","
                       << std::fixed << std::setprecision(6) << baselineAllocation << ","
                       << csvEscape(baselinePath) << ",0.000000,"
                       << summary.samples
                       << ",0.000000,0.000000,0.000000,0.000000\n";
    }

    for (const SweepResult& result : candidateResults) {
      const GrowthPressureSummary candidateSummary = summarizeGrowthPressureRows(result.rows);
      const double score = scoreGrowthPressureSummaryDelta(baselineSummary, candidateSummary);

      const auto candidateByDistrict = summarizeGrowthPressureRowsByDistrict(result.rows);
      std::unordered_set<DistrictId> districtIds;
      for (const auto& [districtId, _] : baselineByDistrict) {
        (void)_;
        districtIds.insert(districtId);
      }
      for (const auto& [districtId, _] : candidateByDistrict) {
        (void)_;
        districtIds.insert(districtId);
      }

      for (DistrictId districtId : districtIds) {
        GrowthPressureSummary baselineDistrictForRow;
        auto baseIt = baselineByDistrict.find(districtId);
        if (baseIt != baselineByDistrict.end()) {
          baselineDistrictForRow = baseIt->second;
        }

        GrowthPressureSummary candidateDistrictSummary;
        auto candidateIt = candidateByDistrict.find(districtId);
        if (candidateIt != candidateByDistrict.end()) {
          candidateDistrictSummary = candidateIt->second;
        }

        const GrowthPressureDelta districtDelta = computeGrowthPressureDelta(
          baselineDistrictForRow,
          candidateDistrictSummary
        );

        std::string districtName = std::to_string(districtId);
        auto nameIt = districtNames.find(districtId);
        if (nameIt != districtNames.end()) {
          districtName = nameIt->second;
        }

        districtManifest << csvEscape(result.label) << ","
                         << result.scenarioSeed << ","
                         << districtId << ","
                         << csvEscape(districtName) << ","
                         << result.cap << ","
                         << std::fixed << std::setprecision(6) << result.allocation << ","
                         << csvEscape(result.path) << ","
                         << std::fixed << std::setprecision(6) << score << ","
                         << candidateDistrictSummary.samples << ","
                         << districtDelta.dMultiplier << ","
                         << districtDelta.dFulfillment << ","
                         << districtDelta.dCapRate << ","
                         << districtDelta.dAllocShare << "\n";
      }
    }

    std::cout << "Wrote district sweep manifest: " << districtManifestPath << "\n";
  }
  return 0;
}
