#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/systems/DistrictSystem.hpp"

// One row of the per-step, per-district growth-pressure report.
struct GrowthPressureReportRow {
  int step = 0;
  DistrictId districtId = 0;
  std::string districtName;
  float multiplier = 1.0f;
  float fulfillment = 1.0f;
  bool capApplied = false;
  int64_t target = 0;
  int64_t allocated = 0;
  uint32_t buildings = 0;
  uint32_t population = 0;
};

struct GrowthPressureSummary {
  int samples = 0;
  int capHits = 0;
  double avgMultiplier = 0.0;
  double avgFulfillment = 0.0;
  double allocationShare = 0.0;
};

struct GrowthPressureDelta {
  double dMultiplier = 0.0;
  double dFulfillment = 0.0;
  double dCapRate = 0.0;
  double dAllocShare = 0.0;
};

// CSV persistence for offline calibration workflows.
bool writeGrowthPressureReportCSV(
  const std::string& filePath,
  const std::vector<GrowthPressureReportRow>& rows
);
bool loadGrowthPressureReportCSV(
  const std::string& filePath,
  std::vector<GrowthPressureReportRow>& outRows,
  std::string& outError
);

// Aggregation and scoring of report rows.
GrowthPressureSummary summarizeGrowthPressureRows(const std::vector<GrowthPressureReportRow>& rows);
GrowthPressureSummary summarizeGrowthPressureRowsForDistrict(
  const std::vector<GrowthPressureReportRow>& rows,
  DistrictId districtId
);
std::unordered_map<DistrictId, GrowthPressureSummary> summarizeGrowthPressureRowsByDistrict(
  const std::vector<GrowthPressureReportRow>& rows
);
double scoreGrowthPressureSummaryDelta(
  const GrowthPressureSummary& baseline,
  const GrowthPressureSummary& candidate
);
GrowthPressureDelta computeGrowthPressureDelta(
  const GrowthPressureSummary& baseline,
  const GrowthPressureSummary& candidate
);

// Report comparison/ranking console output.
void printGrowthPressureComparison(
  const std::string& fileA,
  const std::vector<GrowthPressureReportRow>& rowsA,
  const std::string& fileB,
  const std::vector<GrowthPressureReportRow>& rowsB
);
void printGrowthPressureRanking(
  const std::string& baselinePath,
  const std::vector<GrowthPressureReportRow>& baselineRows,
  const std::vector<std::pair<std::string, std::vector<GrowthPressureReportRow>>>& candidates
);
void printDistrictDeltaRanking(
  const DistrictSystem& districtSystem,
  const std::vector<GrowthPressureReportRow>& baselineRows,
  const std::vector<std::pair<std::string, std::vector<GrowthPressureReportRow>>>& candidates,
  size_t limit
);

// Runs N growth steps, optionally applying district shared-budget pressure
// modifiers, printing per-step output, and collecting report rows.
void runGrowthStepsWithPressure(
  const DistrictSystem& districtSystem,
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  const std::vector<ServiceFacility>& serviceFacilities,
  int runGrowthSteps,
  uint32_t baseSeed,
  int64_t districtPressurePool,
  bool printGrowthPressureFlag,
  bool printStepOutput,
  std::vector<GrowthPressureReportRow>* outRows
);
