#include <iostream>
#include <string>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include "src/core/SimulationTime.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/MetricsSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/persistence/ReplayVerifier.hpp"
#include "src/visualization/MapRenderer.hpp"
#include "src/metrics/CityMetrics.hpp"
#include "src/metrics/GrowthMetrics.hpp"

void printHelp() {
  std::cout << "UrbanSimCore CLI v0.1.0\n"
            << "Usage: UrbanSimCore-cli [options]\n"
            << "Options:\n"
            << "  --size SIZE              Map size (default: 64)\n"
            << "  --ticks N                Number of ticks to simulate (default: 100)\n"
            << "  --seed SEED              Random seed (default: 42)\n"
            << "  --print-map              Print ASCII map representation and exit\n"
            << "  --print-tile X Y         Print detailed info for tile at (X,Y)\n"
            << "  --zone-rect X1 Y1 X2 Y2 TYPE  Apply zoning to rectangle\n"
            << "  --print-zones            Print zoning map and exit\n"
            << "  --print-demand           Print zoning demand stub and exit\n"
            << "  --run-growth N           Run N growth steps and print summary\n"
            << "  --district-pressure-pool POOL  Apply district shared-budget pressure while running growth\n"
            << "  --print-growth-pressure  Print per-step district pressure multipliers during growth\n"
            << "  --export-growth-pressure FILE  Export per-step district pressure CSV for offline calibration\n"
            << "  --compare-growth-pressure FILE_A FILE_B  Compare two growth-pressure CSV reports\n"
            << "  --rank-growth-pressure BASE CANDIDATE  Rank candidate reports vs baseline (repeatable)\n"
            << "  --run-policy-sweep OUT_DIR  Run seed/cap/allocation sweep and auto-rank reports\n"
            << "  --sweep-district DIST_ID  District to mutate during sweep scenarios\n"
            << "  --sweep-seeds A,B,C  Comma-separated seed list for sweep scenarios\n"
            << "  --sweep-caps A,B,C  Comma-separated service cap list (-1 means uncapped)\n"
            << "  --sweep-allocations A,B,C  Comma-separated allocation list (0.0-1.0)\n"
            << "  --sweep-manifest-all-districts  Emit per-scenario per-district breakdown manifest\n"
            << "  --print-growth-summary   Print growth fill-rate summary\n"
            << "  --seed-population N      Allocate N residents to housing/jobs\n"
            << "  --print-population-summary  Print population/job summary\n"
            << "  --print-population-groups   Print grouped population composition\n"
            << "  --print-buildings        Print all spawned buildings\n"
            << "  --place-road X1 Y1 X2 Y2  Build a road segment between tiles\n"
            << "  --connectivity-map       Print connectivity status and exit\n"
            << "  --find-path X1 Y1 X2 Y2  Find shortest path from (X1,Y1) to (X2,Y2)\n"
            << "  --run-commute-simulation Run commute simulation for all employed\n"
            << "  --print-traffic-summary  Print traffic congestion and commute metrics\n"
            << "  --print-top-edges N      Print top N most congested edges\n"
            << "  --traffic-origin X Y     Filter route diagnostics by origin coordinate\n"
            << "  --traffic-destination X Y Filter route diagnostics by destination coordinate\n"
            << "  --run-economy-calculation Run economy/tax calculation\n"
            << "  --print-budget-summary    Print revenue/expense/economic health summary\n"
            << "  --add-service TYPE X Y DIST  Add service facility and max road distance\n"
            << "  --run-service-evaluation  Evaluate service coverage from facilities\n"
            << "  --print-service-summary   Print service coverage and satisfaction\n"
            << "  --print-city-summary      Print consolidated city metrics summary\n"
            << "  --create-district NAME X1 Y1 X2 Y2  Create a district (min to max corners)\n"
            << "  --list-districts         List all districts\n"
            << "  --print-district-summary DIST_ID  Print metrics for a district\n"
            << "  --print-district-balancing POOL  Print all district allocations under shared service budget pool\n"
            << "  --set-district-tax DIST_ID TYPE RATE  Set tax rate (residential|commercial|industrial)\n"
            << "  --set-district-service DIST_ID FIRE POLICE HEALTH EDUCATION  Set service priorities (0-1)\n"
            << "  --set-district-allocation DIST_ID PERCENT  Set district service allocation share (0-1)\n"
            << "  --set-district-budget-cap DIST_ID AMOUNT  Set district service budget cap (negative disables cap)\n"
            << "  --assign-facility DIST_ID FACILITY_ID  Assign service facility to district\n"
            << "  --unassign-facility DIST_ID FACILITY_ID  Remove service facility from district\n"
            << "  --print-district-facilities DIST_ID  Print facilities assigned to district\n"
            << "  --render-map FILE         Render top-down city snapshot to PPM file\n"
            << "  --render-scale N          Pixel size per tile when rendering (default: 8)\n"
            << "  --render-view X Y W H     Render viewport rectangle in tiles\n"
            << "  --save-city FILE          Save city snapshot JSON to FILE\n"
            << "  --load-city FILE          Load city snapshot JSON from FILE (prints migration diagnostics)\n"
            << "  --inspect-snapshot FILE   Inspect snapshot schema and structural summary\n"
            << "  --benchmark-phase5 N      Run N-tick Phase 5 performance benchmark\n"
            << "  --benchmark-phase5-focus PHASE  Time only one phase: ALL|GROWTH|POPULATION|TRAFFIC|ECONOMY|SERVICE\n"
            << "  --verify-replay N         Run deterministic replay check using N growth steps\n"
            << "  --help                   Show this help message\n";
}

enum class BenchmarkFocus {
  All,
  Growth,
  Population,
  Traffic,
  Economy,
  Service
};

bool parseBenchmarkFocus(const std::string& raw, BenchmarkFocus& out) {
  std::string value = raw;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });

  if (value == "ALL") {
    out = BenchmarkFocus::All;
    return true;
  }
  if (value == "GROWTH") {
    out = BenchmarkFocus::Growth;
    return true;
  }
  if (value == "POPULATION") {
    out = BenchmarkFocus::Population;
    return true;
  }
  if (value == "TRAFFIC") {
    out = BenchmarkFocus::Traffic;
    return true;
  }
  if (value == "ECONOMY") {
    out = BenchmarkFocus::Economy;
    return true;
  }
  if (value == "SERVICE") {
    out = BenchmarkFocus::Service;
    return true;
  }
  return false;
}

const char* benchmarkFocusToString(BenchmarkFocus focus) {
  switch (focus) {
    case BenchmarkFocus::All:
      return "ALL";
    case BenchmarkFocus::Growth:
      return "GROWTH";
    case BenchmarkFocus::Population:
      return "POPULATION";
    case BenchmarkFocus::Traffic:
      return "TRAFFIC";
    case BenchmarkFocus::Economy:
      return "ECONOMY";
    case BenchmarkFocus::Service:
      return "SERVICE";
    default:
      return "ALL";
  }
}

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

std::string csvEscape(const std::string& raw) {
  std::string escaped;
  escaped.reserve(raw.size() + 4);
  escaped.push_back('"');
  for (char c : raw) {
    if (c == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(c);
  }
  escaped.push_back('"');
  return escaped;
}

bool writeGrowthPressureReportCSV(
  const std::string& filePath,
  const std::vector<GrowthPressureReportRow>& rows
) {
  std::ofstream out(filePath);
  if (!out.is_open()) {
    return false;
  }

  out << "step,district_id,district_name,multiplier,fulfillment,cap_applied,target,allocated,buildings,population\n";
  for (const GrowthPressureReportRow& row : rows) {
    out << row.step << ","
        << row.districtId << ","
        << csvEscape(row.districtName) << ","
        << std::fixed << std::setprecision(6) << row.multiplier << ","
        << std::fixed << std::setprecision(6) << row.fulfillment << ","
        << (row.capApplied ? 1 : 0) << ","
        << row.target << ","
        << row.allocated << ","
        << row.buildings << ","
        << row.population << "\n";
  }

  return static_cast<bool>(out);
}

std::vector<std::string> parseCSVLine(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool inQuotes = false;

  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
        current.push_back('"');
        ++i;
      } else {
        inQuotes = !inQuotes;
      }
      continue;
    }

    if (c == ',' && !inQuotes) {
      fields.push_back(current);
      current.clear();
      continue;
    }

    current.push_back(c);
  }

  fields.push_back(current);
  return fields;
}

bool loadGrowthPressureReportCSV(
  const std::string& filePath,
  std::vector<GrowthPressureReportRow>& outRows,
  std::string& outError
) {
  std::ifstream in(filePath);
  if (!in.is_open()) {
    outError = "failed to open file";
    return false;
  }

  std::string line;
  if (!std::getline(in, line)) {
    outError = "empty file";
    return false;
  }

  const std::vector<std::string> header = parseCSVLine(line);
  if (header.size() != 10 ||
      header[0] != "step" ||
      header[1] != "district_id" ||
      header[2] != "district_name" ||
      header[3] != "multiplier" ||
      header[4] != "fulfillment" ||
      header[5] != "cap_applied" ||
      header[6] != "target" ||
      header[7] != "allocated" ||
      header[8] != "buildings" ||
      header[9] != "population") {
    outError = "invalid header";
    return false;
  }

  int lineNumber = 1;
  while (std::getline(in, line)) {
    ++lineNumber;
    if (line.empty()) {
      continue;
    }

    const std::vector<std::string> fields = parseCSVLine(line);
    if (fields.size() != 10) {
      outError = "invalid column count at line " + std::to_string(lineNumber);
      return false;
    }

    try {
      GrowthPressureReportRow row;
      row.step = std::stoi(fields[0]);
      row.districtId = static_cast<DistrictId>(std::stoul(fields[1]));
      row.districtName = fields[2];
      row.multiplier = std::stof(fields[3]);
      row.fulfillment = std::stof(fields[4]);
      row.capApplied = (std::stoi(fields[5]) != 0);
      row.target = std::stoll(fields[6]);
      row.allocated = std::stoll(fields[7]);
      row.buildings = static_cast<uint32_t>(std::stoul(fields[8]));
      row.population = static_cast<uint32_t>(std::stoul(fields[9]));
      outRows.push_back(row);
    } catch (...) {
      outError = "parse error at line " + std::to_string(lineNumber);
      return false;
    }
  }

  if (outRows.empty()) {
    outError = "no data rows";
    return false;
  }

  return true;
}

struct GrowthPressureAggregate {
  int samples = 0;
  int capHits = 0;
  int maxStep = 0;
  double sumMultiplier = 0.0;
  double sumFulfillment = 0.0;
  double sumTarget = 0.0;
  double sumAllocated = 0.0;
};

std::unordered_map<std::string, GrowthPressureAggregate> aggregateGrowthPressureRows(
  const std::vector<GrowthPressureReportRow>& rows
) {
  std::unordered_map<std::string, GrowthPressureAggregate> aggregates;
  for (const GrowthPressureReportRow& row : rows) {
    const std::string key = std::to_string(row.districtId) + ":" + row.districtName;
    GrowthPressureAggregate& agg = aggregates[key];
    agg.samples += 1;
    agg.capHits += row.capApplied ? 1 : 0;
    agg.maxStep = std::max(agg.maxStep, row.step);
    agg.sumMultiplier += row.multiplier;
    agg.sumFulfillment += row.fulfillment;
    agg.sumTarget += static_cast<double>(row.target);
    agg.sumAllocated += static_cast<double>(row.allocated);
  }
  return aggregates;
}

void printGrowthPressureComparison(
  const std::string& fileA,
  const std::vector<GrowthPressureReportRow>& rowsA,
  const std::string& fileB,
  const std::vector<GrowthPressureReportRow>& rowsB
) {
  const auto aggA = aggregateGrowthPressureRows(rowsA);
  const auto aggB = aggregateGrowthPressureRows(rowsB);

  std::unordered_set<std::string> keys;
  for (const auto& [key, _] : aggA) {
    (void)_;
    keys.insert(key);
  }
  for (const auto& [key, _] : aggB) {
    (void)_;
    keys.insert(key);
  }

  std::cout << "Growth Pressure Comparison:\n";
  std::cout << "  A: " << fileA << " (rows=" << rowsA.size() << ")\n";
  std::cout << "  B: " << fileB << " (rows=" << rowsB.size() << ")\n";

  if (keys.empty()) {
    std::cout << "  No comparable district data.\n";
    return;
  }

  for (const std::string& key : keys) {
    auto itA = aggA.find(key);
    auto itB = aggB.find(key);
    const bool hasA = itA != aggA.end();
    const bool hasB = itB != aggB.end();

    std::cout << "  District " << key << ": ";
    if (!hasA || !hasB) {
      std::cout << "missing in " << (hasA ? "B" : "A") << "\n";
      continue;
    }

    const GrowthPressureAggregate& a = itA->second;
    const GrowthPressureAggregate& b = itB->second;

    const double avgMultiplierA = a.samples > 0 ? (a.sumMultiplier / static_cast<double>(a.samples)) : 0.0;
    const double avgMultiplierB = b.samples > 0 ? (b.sumMultiplier / static_cast<double>(b.samples)) : 0.0;
    const double avgFulfillmentA = a.samples > 0 ? (a.sumFulfillment / static_cast<double>(a.samples)) : 0.0;
    const double avgFulfillmentB = b.samples > 0 ? (b.sumFulfillment / static_cast<double>(b.samples)) : 0.0;
    const double capRateA = a.samples > 0 ? (static_cast<double>(a.capHits) / static_cast<double>(a.samples)) : 0.0;
    const double capRateB = b.samples > 0 ? (static_cast<double>(b.capHits) / static_cast<double>(b.samples)) : 0.0;
    const double allocationShareA = a.sumTarget > 0.0 ? (a.sumAllocated / a.sumTarget) : 0.0;
    const double allocationShareB = b.sumTarget > 0.0 ? (b.sumAllocated / b.sumTarget) : 0.0;

    std::cout << std::fixed << std::setprecision(3)
              << "dMultiplier=" << (avgMultiplierB - avgMultiplierA)
              << " dFulfillment=" << (avgFulfillmentB - avgFulfillmentA)
              << " dCapRate=" << (capRateB - capRateA)
              << " dAllocShare=" << (allocationShareB - allocationShareA)
              << "\n";
  }
}

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

GrowthPressureSummary summarizeGrowthPressureRows(const std::vector<GrowthPressureReportRow>& rows) {
  GrowthPressureSummary summary;
  if (rows.empty()) {
    return summary;
  }

  double sumMultiplier = 0.0;
  double sumFulfillment = 0.0;
  double sumTarget = 0.0;
  double sumAllocated = 0.0;

  summary.samples = static_cast<int>(rows.size());
  for (const GrowthPressureReportRow& row : rows) {
    sumMultiplier += static_cast<double>(row.multiplier);
    sumFulfillment += static_cast<double>(row.fulfillment);
    sumTarget += static_cast<double>(row.target);
    sumAllocated += static_cast<double>(row.allocated);
    if (row.capApplied) {
      summary.capHits += 1;
    }
  }

  summary.avgMultiplier = sumMultiplier / static_cast<double>(summary.samples);
  summary.avgFulfillment = sumFulfillment / static_cast<double>(summary.samples);
  summary.allocationShare = sumTarget > 0.0 ? (sumAllocated / sumTarget) : 0.0;
  return summary;
}

double scoreGrowthPressureSummaryDelta(
  const GrowthPressureSummary& baseline,
  const GrowthPressureSummary& candidate
) {
  const double baselineCapRate = baseline.samples > 0
    ? (static_cast<double>(baseline.capHits) / static_cast<double>(baseline.samples))
    : 0.0;
  const double candidateCapRate = candidate.samples > 0
    ? (static_cast<double>(candidate.capHits) / static_cast<double>(candidate.samples))
    : 0.0;

  const double dMultiplier = candidate.avgMultiplier - baseline.avgMultiplier;
  const double dFulfillment = candidate.avgFulfillment - baseline.avgFulfillment;
  const double dCapRate = candidateCapRate - baselineCapRate;
  const double dAllocShare = candidate.allocationShare - baseline.allocationShare;

  // Positive score is better: higher fulfillment/allocation with lower cap-hit rate.
  return (0.50 * dFulfillment) + (0.25 * dAllocShare) + (0.10 * dMultiplier) - (0.35 * dCapRate);
}

GrowthPressureDelta computeGrowthPressureDelta(
  const GrowthPressureSummary& baseline,
  const GrowthPressureSummary& candidate
) {
  const double baselineCapRate = baseline.samples > 0
    ? (static_cast<double>(baseline.capHits) / static_cast<double>(baseline.samples))
    : 0.0;
  const double candidateCapRate = candidate.samples > 0
    ? (static_cast<double>(candidate.capHits) / static_cast<double>(candidate.samples))
    : 0.0;

  GrowthPressureDelta delta;
  delta.dMultiplier = candidate.avgMultiplier - baseline.avgMultiplier;
  delta.dFulfillment = candidate.avgFulfillment - baseline.avgFulfillment;
  delta.dCapRate = candidateCapRate - baselineCapRate;
  delta.dAllocShare = candidate.allocationShare - baseline.allocationShare;
  return delta;
}

GrowthPressureSummary summarizeGrowthPressureRowsForDistrict(
  const std::vector<GrowthPressureReportRow>& rows,
  DistrictId districtId
) {
  GrowthPressureSummary summary;

  double sumMultiplier = 0.0;
  double sumFulfillment = 0.0;
  double sumTarget = 0.0;
  double sumAllocated = 0.0;

  for (const GrowthPressureReportRow& row : rows) {
    if (row.districtId != districtId) {
      continue;
    }
    summary.samples += 1;
    sumMultiplier += static_cast<double>(row.multiplier);
    sumFulfillment += static_cast<double>(row.fulfillment);
    sumTarget += static_cast<double>(row.target);
    sumAllocated += static_cast<double>(row.allocated);
    if (row.capApplied) {
      summary.capHits += 1;
    }
  }

  if (summary.samples == 0) {
    return summary;
  }

  summary.avgMultiplier = sumMultiplier / static_cast<double>(summary.samples);
  summary.avgFulfillment = sumFulfillment / static_cast<double>(summary.samples);
  summary.allocationShare = sumTarget > 0.0 ? (sumAllocated / sumTarget) : 0.0;
  return summary;
}

std::unordered_map<DistrictId, GrowthPressureSummary> summarizeGrowthPressureRowsByDistrict(
  const std::vector<GrowthPressureReportRow>& rows
) {
  struct Accumulator {
    int samples = 0;
    int capHits = 0;
    double sumMultiplier = 0.0;
    double sumFulfillment = 0.0;
    double sumTarget = 0.0;
    double sumAllocated = 0.0;
  };

  std::unordered_map<DistrictId, Accumulator> accumulators;
  for (const GrowthPressureReportRow& row : rows) {
    Accumulator& acc = accumulators[row.districtId];
    acc.samples += 1;
    acc.capHits += row.capApplied ? 1 : 0;
    acc.sumMultiplier += static_cast<double>(row.multiplier);
    acc.sumFulfillment += static_cast<double>(row.fulfillment);
    acc.sumTarget += static_cast<double>(row.target);
    acc.sumAllocated += static_cast<double>(row.allocated);
  }

  std::unordered_map<DistrictId, GrowthPressureSummary> result;
  result.reserve(accumulators.size());
  for (const auto& [districtId, acc] : accumulators) {
    GrowthPressureSummary summary;
    summary.samples = acc.samples;
    summary.capHits = acc.capHits;
    if (summary.samples > 0) {
      summary.avgMultiplier = acc.sumMultiplier / static_cast<double>(summary.samples);
      summary.avgFulfillment = acc.sumFulfillment / static_cast<double>(summary.samples);
      summary.allocationShare = acc.sumTarget > 0.0 ? (acc.sumAllocated / acc.sumTarget) : 0.0;
    }
    result[districtId] = summary;
  }

  return result;
}

void printGrowthPressureRanking(
  const std::string& baselinePath,
  const std::vector<GrowthPressureReportRow>& baselineRows,
  const std::vector<std::pair<std::string, std::vector<GrowthPressureReportRow>>>& candidates
) {
  struct RankedResult {
    std::string path;
    double score = 0.0;
    double dMultiplier = 0.0;
    double dFulfillment = 0.0;
    double dCapRate = 0.0;
    double dAllocShare = 0.0;
    int rows = 0;
  };

  const GrowthPressureSummary baseline = summarizeGrowthPressureRows(baselineRows);
  const double baselineCapRate = baseline.samples > 0
    ? (static_cast<double>(baseline.capHits) / static_cast<double>(baseline.samples))
    : 0.0;

  std::vector<RankedResult> ranked;
  ranked.reserve(candidates.size());

  for (const auto& [path, rows] : candidates) {
    const GrowthPressureSummary candidate = summarizeGrowthPressureRows(rows);
    const double candidateCapRate = candidate.samples > 0
      ? (static_cast<double>(candidate.capHits) / static_cast<double>(candidate.samples))
      : 0.0;

    RankedResult result;
    result.path = path;
    result.rows = static_cast<int>(rows.size());
    result.dMultiplier = candidate.avgMultiplier - baseline.avgMultiplier;
    result.dFulfillment = candidate.avgFulfillment - baseline.avgFulfillment;
    result.dCapRate = candidateCapRate - baselineCapRate;
    result.dAllocShare = candidate.allocationShare - baseline.allocationShare;
    result.score = scoreGrowthPressureSummaryDelta(baseline, candidate);
    ranked.push_back(result);
  }

  std::sort(ranked.begin(), ranked.end(), [](const RankedResult& a, const RankedResult& b) {
    if (a.score == b.score) {
      return a.path < b.path;
    }
    return a.score > b.score;
  });

  std::cout << "Growth Pressure Ranking:\n";
  std::cout << "  Baseline: " << baselinePath << " (rows=" << baselineRows.size() << ")\n";
  for (size_t i = 0; i < ranked.size(); ++i) {
    const RankedResult& r = ranked[i];
    std::cout << "  #" << (i + 1) << " " << r.path
              << " score=" << std::fixed << std::setprecision(4) << r.score
              << " dMult=" << std::setprecision(3) << r.dMultiplier
              << " dFulfill=" << r.dFulfillment
              << " dCapRate=" << r.dCapRate
              << " dAllocShare=" << r.dAllocShare
              << " rows=" << r.rows << "\n";
  }
}

void printDistrictDeltaRanking(
  const std::vector<GrowthPressureReportRow>& baselineRows,
  const std::vector<std::pair<std::string, std::vector<GrowthPressureReportRow>>>& candidates,
  size_t limit
) {
  if (candidates.empty()) {
    return;
  }

  struct DistrictAccumulator {
    int scenarios = 0;
    double sumScore = 0.0;
    double sumMultiplier = 0.0;
    double sumFulfillment = 0.0;
    double sumCapRate = 0.0;
    double sumAllocShare = 0.0;
  };

  struct DistrictRankRow {
    DistrictId districtId = 0;
    std::string districtName;
    int scenarios = 0;
    double avgScore = 0.0;
    double avgMultiplier = 0.0;
    double avgFulfillment = 0.0;
    double avgCapRate = 0.0;
    double avgAllocShare = 0.0;
  };

  std::unordered_map<DistrictId, std::string> districtNames;
  for (const District& district : DistrictSystem::getDistricts()) {
    districtNames[district.id] = district.name;
  }

  const auto baselineByDistrict = summarizeGrowthPressureRowsByDistrict(baselineRows);
  std::unordered_map<DistrictId, DistrictAccumulator> accumulators;

  for (const auto& [path, rows] : candidates) {
    (void)path;
    const auto candidateByDistrict = summarizeGrowthPressureRowsByDistrict(rows);
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
      GrowthPressureSummary baselineSummary;
      auto baseIt = baselineByDistrict.find(districtId);
      if (baseIt != baselineByDistrict.end()) {
        baselineSummary = baseIt->second;
      }

      GrowthPressureSummary candidateSummary;
      auto candidateIt = candidateByDistrict.find(districtId);
      if (candidateIt != candidateByDistrict.end()) {
        candidateSummary = candidateIt->second;
      }

      const GrowthPressureDelta delta = computeGrowthPressureDelta(baselineSummary, candidateSummary);
      const double score = scoreGrowthPressureSummaryDelta(baselineSummary, candidateSummary);

      DistrictAccumulator& acc = accumulators[districtId];
      acc.scenarios += 1;
      acc.sumScore += score;
      acc.sumMultiplier += delta.dMultiplier;
      acc.sumFulfillment += delta.dFulfillment;
      acc.sumCapRate += delta.dCapRate;
      acc.sumAllocShare += delta.dAllocShare;
    }
  }

  std::vector<DistrictRankRow> ranked;
  ranked.reserve(accumulators.size());
  for (const auto& [districtId, acc] : accumulators) {
    if (acc.scenarios <= 0) {
      continue;
    }

    DistrictRankRow row;
    row.districtId = districtId;
    auto nameIt = districtNames.find(districtId);
    row.districtName = (nameIt != districtNames.end()) ? nameIt->second : std::to_string(districtId);
    row.scenarios = acc.scenarios;
    row.avgScore = acc.sumScore / static_cast<double>(acc.scenarios);
    row.avgMultiplier = acc.sumMultiplier / static_cast<double>(acc.scenarios);
    row.avgFulfillment = acc.sumFulfillment / static_cast<double>(acc.scenarios);
    row.avgCapRate = acc.sumCapRate / static_cast<double>(acc.scenarios);
    row.avgAllocShare = acc.sumAllocShare / static_cast<double>(acc.scenarios);
    ranked.push_back(row);
  }

  if (ranked.empty()) {
    return;
  }

  std::sort(ranked.begin(), ranked.end(), [](const DistrictRankRow& a, const DistrictRankRow& b) {
    if (a.avgScore == b.avgScore) {
      return a.districtId < b.districtId;
    }
    return a.avgScore > b.avgScore;
  });

  const size_t topCount = std::min(limit, ranked.size());
  std::cout << "District Delta Ranking (avg vs baseline across " << candidates.size() << " scenarios):\n";
  std::cout << "  Top districts:\n";
  for (size_t i = 0; i < topCount; ++i) {
    const DistrictRankRow& row = ranked[i];
    std::cout << "    #" << (i + 1)
              << " D" << row.districtId << " " << row.districtName
              << " avgScore=" << std::fixed << std::setprecision(4) << row.avgScore
              << " dFulfill=" << std::setprecision(3) << row.avgFulfillment
              << " dCapRate=" << row.avgCapRate
              << " dAllocShare=" << row.avgAllocShare
              << " scenarios=" << row.scenarios << "\n";
  }

  size_t bottomCount = std::min(limit, ranked.size());
  if (ranked.size() <= topCount) {
    bottomCount = std::min<size_t>(1, ranked.size());
  }
  std::cout << "  Bottom districts:\n";
  for (size_t i = 0; i < bottomCount; ++i) {
    const size_t idx = ranked.size() - 1 - i;

    const DistrictRankRow& row = ranked[idx];
    std::cout << "    #" << (i + 1)
              << " D" << row.districtId << " " << row.districtName
              << " avgScore=" << std::fixed << std::setprecision(4) << row.avgScore
              << " dFulfill=" << std::setprecision(3) << row.avgFulfillment
              << " dCapRate=" << row.avgCapRate
              << " dAllocShare=" << row.avgAllocShare
              << " scenarios=" << row.scenarios << "\n";
  }
}

std::string trimString(const std::string& raw) {
  size_t begin = 0;
  while (begin < raw.size() && std::isspace(static_cast<unsigned char>(raw[begin])) != 0) {
    ++begin;
  }

  size_t end = raw.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1])) != 0) {
    --end;
  }

  return raw.substr(begin, end - begin);
}

bool parseUint32List(const std::string& raw, std::vector<uint32_t>& outValues) {
  outValues.clear();

  std::stringstream ss(raw);
  std::string token;
  while (std::getline(ss, token, ',')) {
    const std::string trimmed = trimString(token);
    if (trimmed.empty()) {
      continue;
    }

    try {
      const unsigned long value = std::stoul(trimmed);
      outValues.push_back(static_cast<uint32_t>(value));
    } catch (...) {
      return false;
    }
  }

  return !outValues.empty();
}

bool parseInt64List(const std::string& raw, std::vector<int64_t>& outValues) {
  outValues.clear();

  std::stringstream ss(raw);
  std::string token;
  while (std::getline(ss, token, ',')) {
    const std::string trimmed = trimString(token);
    if (trimmed.empty()) {
      continue;
    }

    try {
      const long long value = std::stoll(trimmed);
      outValues.push_back(static_cast<int64_t>(value));
    } catch (...) {
      return false;
    }
  }

  return !outValues.empty();
}

bool parseFloatList(const std::string& raw, std::vector<float>& outValues) {
  outValues.clear();

  std::stringstream ss(raw);
  std::string token;
  while (std::getline(ss, token, ',')) {
    const std::string trimmed = trimString(token);
    if (trimmed.empty()) {
      continue;
    }

    try {
      const float value = std::stof(trimmed);
      outValues.push_back(value);
    } catch (...) {
      return false;
    }
  }

  return !outValues.empty();
}

std::string sanitizeToken(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  return out;
}

void runGrowthStepsWithPressure(
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
) {
  if (districtPressurePool >= 0 && printStepOutput) {
    std::cout << "Applying district growth pressure with shared pool "
              << districtPressurePool << "\n";
  }

  if (outRows != nullptr) {
    outRows->clear();
    outRows->reserve(static_cast<size_t>(runGrowthSteps) * std::max<size_t>(1, DistrictSystem::getDistricts().size()));
  }

  for (int step = 0; step < runGrowthSteps; ++step) {
    const ZoneDemand demand = Zoning::calculateDemand(baseSeed + static_cast<uint32_t>(step));

    std::vector<GrowthChanceModifier> growthModifiers;
    if (districtPressurePool >= 0) {
      const auto balancedMetrics = DistrictSystem::evaluateAllDistricts(
        map,
        store,
        population,
        &roads,
        &serviceFacilities,
        districtPressurePool
      );

      std::unordered_map<DistrictId, float> districtChanceMultiplier;
      for (const DistrictMetrics& metrics : balancedMetrics) {
        const District* district = DistrictSystem::getDistrictConst(metrics.districtId);
        if (district == nullptr) {
          continue;
        }
        districtChanceMultiplier[metrics.districtId] =
          DistrictSystem::computeGrowthPressureMultiplier(*district, metrics);
      }

      if (printGrowthPressureFlag) {
        std::cout << "  Pressure step " << (step + 1) << ": ";
        bool first = true;
        for (const DistrictMetrics& metrics : balancedMetrics) {
          auto it = districtChanceMultiplier.find(metrics.districtId);
          if (it == districtChanceMultiplier.end()) {
            continue;
          }
          if (!first) {
            std::cout << " | ";
          }

          float fulfillment = 1.0f;
          if (metrics.serviceBudgetTarget > 0) {
            fulfillment = static_cast<float>(metrics.serviceBudgetAllocated) /
              static_cast<float>(metrics.serviceBudgetTarget);
          }
          fulfillment = std::max(0.0f, std::min(1.0f, fulfillment));

          std::cout << "D" << metrics.districtId
                    << " m=" << std::fixed << std::setprecision(2) << it->second
                    << " f=" << std::setprecision(2) << fulfillment
                    << " cap=" << (metrics.serviceBudgetCapApplied ? "y" : "n");
          first = false;
        }
        std::cout << "\n";
      }

      if (outRows != nullptr) {
        for (const DistrictMetrics& metrics : balancedMetrics) {
          auto it = districtChanceMultiplier.find(metrics.districtId);
          if (it == districtChanceMultiplier.end()) {
            continue;
          }

          float fulfillment = 1.0f;
          if (metrics.serviceBudgetTarget > 0) {
            fulfillment = static_cast<float>(metrics.serviceBudgetAllocated) /
              static_cast<float>(metrics.serviceBudgetTarget);
          }
          fulfillment = std::max(0.0f, std::min(1.0f, fulfillment));

          GrowthPressureReportRow row;
          row.step = step + 1;
          row.districtId = metrics.districtId;
          row.districtName = metrics.districtName;
          row.multiplier = it->second;
          row.fulfillment = fulfillment;
          row.capApplied = metrics.serviceBudgetCapApplied;
          row.target = metrics.serviceBudgetTarget;
          row.allocated = metrics.serviceBudgetAllocated;
          row.buildings = metrics.buildings;
          row.population = metrics.population;
          outRows->push_back(row);
        }
      }

      const auto& districts = DistrictSystem::getDistricts();
      growthModifiers.reserve(districts.size());
      for (const District& district : districts) {
        auto it = districtChanceMultiplier.find(district.id);
        if (it == districtChanceMultiplier.end()) {
          continue;
        }

        GrowthChanceModifier modifier;
        modifier.minCorner = district.minCorner;
        modifier.maxCorner = district.maxCorner;
        modifier.multiplier = it->second;
        growthModifiers.push_back(modifier);
      }
    }

    const GrowthStats stats = GrowthSystem::runStep(
      map,
      roads,
      store,
      demand,
      baseSeed + static_cast<uint32_t>(step),
      0.5f,
      growthModifiers.empty() ? nullptr : &growthModifiers
    );

    if (printStepOutput) {
      std::cout << "Growth step " << (step + 1)
                << ": evaluated=" << stats.evaluatedTiles
                << " spawned=" << stats.totalSpawned()
                << " (R=" << stats.spawnedResidential
                << ", C=" << stats.spawnedCommercial
                << ", I=" << stats.spawnedIndustrial << ")"
                << " demolished=" << stats.totalDemolished()
                << " (R=" << stats.demolishedResidential
                << ", C=" << stats.demolishedCommercial
                << ", I=" << stats.demolishedIndustrial << ")\n";
    }
  }
}

void printServiceSummary(const ServiceCoverageSummary& summary) {
  std::cout << "Service Summary:\n";
  std::cout << "  Total Buildings: " << summary.totalBuildings << "\n";
  std::cout << "  Serviced Buildings: " << summary.servicedBuildings << "\n";
  std::cout << "  Fire Coverage: " << std::fixed << std::setprecision(1)
            << (summary.fireCoverage * 100.0f) << "%\n";
  std::cout << "  Police Coverage: " << std::fixed << std::setprecision(1)
            << (summary.policeCoverage * 100.0f) << "%\n";
  std::cout << "  Health Coverage: " << std::fixed << std::setprecision(1)
            << (summary.healthCoverage * 100.0f) << "%\n";
  std::cout << "  Education Coverage: " << std::fixed << std::setprecision(1)
            << (summary.educationCoverage * 100.0f) << "%\n";
  std::cout << "  Overall Coverage: " << std::fixed << std::setprecision(1)
            << (summary.overallCoverage * 100.0f) << "%\n";
  std::cout << "  Satisfaction: " << std::fixed << std::setprecision(1)
            << (summary.satisfaction * 100.0f) << "%\n";
}

PopulationSummary buildPopulationSummaryFromState(
  const EntityStore& store,
  const PopulationStore& population
) {
  PopulationSummary summary;
  summary.requestedPopulation = population.getTotalPopulation();
  summary.housedPopulation = summary.requestedPopulation;
  summary.employedPopulation = population.getTotalEmployed();
  summary.unemployedPopulation = summary.housedPopulation - summary.employedPopulation;

  uint32_t housingCapacity = 0;
  uint32_t jobCapacity = 0;
  uint32_t occupiedHousing = 0;
  uint32_t occupiedJobs = 0;

  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    const uint32_t cap = static_cast<uint32_t>(std::max(0, building.capacity));
    const uint32_t occ = static_cast<uint32_t>(std::max(0, building.occupancy));
    if (building.type == BuildingType::Residential) {
      housingCapacity += cap;
      occupiedHousing += std::min(cap, occ);
    } else {
      jobCapacity += cap;
      occupiedJobs += std::min(cap, occ);
    }
  }

  summary.availableHousing = (housingCapacity > occupiedHousing) ? (housingCapacity - occupiedHousing) : 0;
  summary.availableJobs = (jobCapacity > occupiedJobs) ? (jobCapacity - occupiedJobs) : 0;
  summary.unemploymentRate = summary.housedPopulation > 0
    ? (static_cast<float>(summary.unemployedPopulation) / static_cast<float>(summary.housedPopulation))
    : 0.0f;

  for (const auto& [id, group] : population.getGroups()) {
    (void)id;
    switch (group.band) {
      case IncomeBand::Low:
        summary.lowIncomePopulation += group.size;
        summary.lowIncomeEmployed += group.employed;
        break;
      case IncomeBand::Middle:
        summary.middleIncomePopulation += group.size;
        summary.middleIncomeEmployed += group.employed;
        break;
      case IncomeBand::High:
        summary.highIncomePopulation += group.size;
        summary.highIncomeEmployed += group.employed;
        break;
    }
  }

  return summary;
}

void printMap(const CityMap& map) {
  glm::ivec2 dims = map.getDimensions();
  
  std::cout << "Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = empty, # = has road, ~ = water, T = terrain\n\n";
  
  // Print column headers
  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";
  
  // Print rows
  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      char symbol = '.';
      if (tile.hasRoad) symbol = '#';
      else if (tile.type == 1) symbol = 'T';
      else if (tile.type == 2) symbol = '~';
      std::cout << symbol;
    }
    std::cout << "\n";
  }
}

void printTile(const CityMap& map, int x, int y) {
  if (!map.isValid({x, y})) {
    std::cout << "Error: Tile (" << x << "," << y << ") is out of bounds\n";
    return;
  }
  
  const Tile& tile = map.getTile({x, y});
  
  std::cout << "Tile (" << x << "," << y << "):\n";
  std::cout << "  Position: (" << tile.position.x << "," << tile.position.y << ")\n";
  std::cout << "  Zone: " << Zoning::zoneToString(tile.zone) << " (" << tile.zone << ")\n";
  std::cout << "  Type: " << static_cast<int>(tile.type) << "\n";
  std::cout << "  Land Value: " << tile.landValue << "\n";
  std::cout << "  Pollution: " << tile.pollution << "\n";
  std::cout << "  Has Road: " << (tile.hasRoad ? "Yes" : "No") << "\n";
  std::cout << "  Connected to Road: " << (tile.connectedToRoad ? "Yes" : "No") << "\n";
  if (tile.buildingId != 0) {
    std::cout << "  Building ID: " << tile.buildingId << "\n";
  }
}

void printZones(const CityMap& map) {
  glm::ivec2 dims = map.getDimensions();

  std::cout << "Zone Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = none, R = residential, C = commercial, I = industrial, P = park\n\n";

  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";

  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      std::cout << Zoning::zoneToSymbol(tile.zone);
    }
    std::cout << "\n";
  }
}

void printDemand(uint32_t seed) {
  ZoneDemand demand = Zoning::calculateDemand(seed);
  std::cout << "Zone demand (stub):\n";
  std::cout << "  Residential: " << std::fixed << std::setprecision(3) << demand.residential << "\n";
  std::cout << "  Commercial:  " << std::fixed << std::setprecision(3) << demand.commercial << "\n";
  std::cout << "  Industrial:  " << std::fixed << std::setprecision(3) << demand.industrial << "\n";
}

const char* buildingTypeToString(BuildingType type) {
  switch (type) {
    case BuildingType::Residential:
      return "Residential";
    case BuildingType::Commercial:
      return "Commercial";
    case BuildingType::Industrial:
      return "Industrial";
    default:
      return "Unknown";
  }
}

void printBuildings(const EntityStore& store) {
  const auto& buildings = store.getBuildings();
  std::cout << "Buildings: " << buildings.size() << "\n";

  for (const auto& [id, building] : buildings) {
    std::cout << "  #" << id << " " << buildingTypeToString(building.type)
              << " at (" << building.position.x << "," << building.position.y << ")"
              << " cap=" << building.capacity << " occ=" << building.occupancy << "\n";
  }
}

void printConnectivityMap(const CityMap& map, const RoadNetwork& network) {
  glm::ivec2 dims = map.getDimensions();
  
  std::cout << "Connectivity Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = disconnected, X = connected, # = road only\n\n";
  
  // Print column headers
  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";
  
  // Print rows
  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      char symbol = '.';
      if (network.isConnected({x, y})) {
        symbol = 'X';
      } else if (map.getTile({x, y}).hasRoad) {
        symbol = '#';
      }
      std::cout << symbol;
    }
    std::cout << "\n";
  }
}

void printPath(const Pathfinding::Path& path) {
  if (!path.found) {
    std::cout << "No path found between points.\n";
    return;
  }
  
  std::cout << "Path found (distance: " << std::fixed << std::setprecision(1) 
            << path.totalDistance << "):\n";
  for (size_t i = 0; i < path.waypoints.size(); ++i) {
    if (i > 0) std::cout << " -> ";
    std::cout << "(" << path.waypoints[i].x << "," << path.waypoints[i].y << ")";
  }
  std::cout << "\n";
}

const char* incomeBandToString(IncomeBand band) {
  switch (band) {
    case IncomeBand::Low:
      return "Low";
    case IncomeBand::Middle:
      return "Middle";
    case IncomeBand::High:
      return "High";
    default:
      return "Unknown";
  }
}

void printPopulationSummary(const PopulationSummary& summary) {
  std::cout << "Population Summary:\n";
  std::cout << "  Requested: " << summary.requestedPopulation << "\n";
  std::cout << "  Housed: " << summary.housedPopulation << "\n";
  std::cout << "  Employed: " << summary.employedPopulation << "\n";
  std::cout << "  Unemployed: " << summary.unemployedPopulation << "\n";
  std::cout << "  Available Housing: " << summary.availableHousing << "\n";
  std::cout << "  Available Jobs: " << summary.availableJobs << "\n";
  std::cout << "  Unemployment: " << std::fixed << std::setprecision(1)
            << (summary.unemploymentRate * 100.0f) << "%\n";
  std::cout << "  Composition (pop): Low=" << summary.lowIncomePopulation
            << ", Middle=" << summary.middleIncomePopulation
            << ", High=" << summary.highIncomePopulation << "\n";
  std::cout << "  Composition (employed): Low=" << summary.lowIncomeEmployed
            << ", Middle=" << summary.middleIncomeEmployed
            << ", High=" << summary.highIncomeEmployed << "\n";
}

void printPopulationGroups(const PopulationStore& population) {
  std::cout << "Population Groups: " << population.getGroupCount() << "\n";
  for (const auto& [id, group] : population.getGroups()) {
    std::cout << "  #" << id << " " << incomeBandToString(group.band)
              << " size=" << group.size
              << " employed=" << group.employed << "\n";
  }
}

void printTrafficSummary(const TrafficSummary& summary) {
  std::cout << "Traffic Summary:\n";
  std::cout << "  Commuting Population: " << summary.commutingPopulation << "\n";
  std::cout << "  Average Commute Time: " << std::fixed << std::setprecision(2)
            << summary.averageCommuteTime << "\n";
  std::cout << "  Total Commute Burden: " << summary.totalCommuteBurden << "\n";
  std::cout << "  Max Edge Congestion: " << std::fixed << std::setprecision(3)
            << (summary.maxEdgeCongestion * 100.0f) << "%\n";
  std::cout << "  Average Edge Congestion: " << std::fixed << std::setprecision(3)
            << (summary.averageEdgeCongestion * 100.0f) << "%\n";
  std::cout << "  Congested Edges: " << summary.congestionDetectedEdges << "\n";
}

void printTopCongestedEdges(const std::vector<EdgeTrafficData>& edges) {
  std::cout << "Top Congested Edges:\n";
  for (size_t i = 0; i < edges.size(); ++i) {
    const auto& edge = edges[i];
    std::cout << "  #" << (i + 1) << " (" << edge.from.x << "," << edge.from.y
              << ")->(" << edge.to.x << "," << edge.to.y << ")"
              << " congestion=" << std::fixed << std::setprecision(1)
              << (edge.congestion * 100.0f) << "%"
              << " commuters=" << edge.totalCommuters << "\n";
  }
}

void printRouteDiagnosticsFilter(const RouteDiagnosticsFilter& filter) {
  if (!filter.hasOrigin && !filter.hasDestination) {
    return;
  }

  std::cout << "Route Diagnostics Filter:";
  if (filter.hasOrigin) {
    std::cout << " origin=(" << filter.origin.x << "," << filter.origin.y << ")";
  }
  if (filter.hasDestination) {
    std::cout << " destination=(" << filter.destination.x << "," << filter.destination.y << ")";
  }
  std::cout << "\n";
}

void printBudgetSummary(const EconomyState& economy) {
  std::cout << "Budget Summary:\n";
  std::cout << "  Residential Tax Revenue: " << economy.residentialTaxRevenue << "\n";
  std::cout << "  Commercial Tax Revenue: " << economy.commercialTaxRevenue << "\n";
  std::cout << "  Industrial Tax Revenue: " << economy.industrialTaxRevenue << "\n";
  std::cout << "  Total Revenue: " << economy.totalRevenue << "\n";
  std::cout << "  Residential Maintenance: " << economy.residentialMaintenance << "\n";
  std::cout << "  Commercial Maintenance: " << economy.commercialMaintenance << "\n";
  std::cout << "  Industrial Maintenance: " << economy.industrialMaintenance << "\n";
  std::cout << "  Total Expenses: " << economy.totalExpenses << "\n";
  std::cout << "  Balance: " << economy.balance << "\n";
  std::cout << "  Average Land Value: " << std::fixed << std::setprecision(2)
            << economy.averageLandValue << "\n";
  std::cout << "  Economic Health: " << std::fixed << std::setprecision(1)
            << (economy.economicHealth * 100.0f) << "%\n";
}

void printSnapshotInspection(const CitySnapshot& snapshot, const SnapshotLoadDiagnostics& diagnostics) {
  size_t roadTileCount = 0;
  size_t zonedTileCount = 0;
  std::array<size_t, 5> zoneHistogram = {0, 0, 0, 0, 0};

  for (const SerializedTile& tile : snapshot.tiles) {
    if (tile.hasRoad) {
      ++roadTileCount;
    }
    if (tile.zone > 0 && tile.zone < static_cast<int>(zoneHistogram.size())) {
      ++zonedTileCount;
      ++zoneHistogram[static_cast<size_t>(tile.zone)];
    }
  }

  size_t residentialBuildings = 0;
  size_t commercialBuildings = 0;
  size_t industrialBuildings = 0;
  for (const Building& building : snapshot.buildings) {
    switch (building.type) {
      case BuildingType::Residential:
        ++residentialBuildings;
        break;
      case BuildingType::Commercial:
        ++commercialBuildings;
        break;
      case BuildingType::Industrial:
        ++industrialBuildings;
        break;
    }
  }

  std::cout << "Snapshot Inspection:\n";
  std::cout << "  Schema: sourceVersion=" << diagnostics.sourceVersion
            << ", targetVersion=" << diagnostics.targetVersion
            << ", migrated=" << (diagnostics.migrationApplied ? "yes" : "no")
            << ", path=" << diagnostics.migrationPath << "\n";
  std::cout << "  Map: " << snapshot.width << "x" << snapshot.height
            << " tiles=" << snapshot.tiles.size()
            << " roads=" << snapshot.roads.size()
            << " roadTiles=" << roadTileCount << "\n";
  std::cout << "  Zoning: zonedTiles=" << zonedTileCount
            << " (R=" << zoneHistogram[1]
            << " C=" << zoneHistogram[2]
            << " I=" << zoneHistogram[3]
            << " P=" << zoneHistogram[4] << ")\n";
  std::cout << "  Buildings: total=" << snapshot.buildings.size()
            << " (R=" << residentialBuildings
            << " C=" << commercialBuildings
            << " I=" << industrialBuildings << ")\n";
  std::cout << "  Population Groups: " << snapshot.populationGroups.size() << "\n";
}

void seedBenchmarkScenario(CityMap& map, RoadNetwork& roads) {
  const glm::ivec2 dims = map.getDimensions();
  const int midX = dims.x / 2;
  const int midY = dims.y / 2;

  Zoning::applyZoneRect(map, {2, 2}, {midX - 2, dims.y - 3}, ZoneType::Residential);
  Zoning::applyZoneRect(map, {midX + 1, 2}, {dims.x - 3, midY - 2}, ZoneType::Commercial);
  Zoning::applyZoneRect(map, {midX + 1, midY + 1}, {dims.x - 3, dims.y - 3}, ZoneType::Industrial);

  for (int x = 1; x < dims.x - 1; ++x) {
    roads.buildRoad({x, midY}, {x + 1, midY});
  }
  for (int y = 1; y < dims.y - 1; ++y) {
    roads.buildRoad({midX, y}, {midX, y + 1});
  }

  for (int x = 6; x < dims.x - 6; x += 8) {
    roads.buildRoad({x, 6}, {x, 7});
    roads.buildRoad({x, dims.y - 7}, {x, dims.y - 8});
  }

  roads.updateConnectivity({midX, midY});
}

void printBenchmarkResults(
  int benchmarkTicks,
  BenchmarkFocus focus,
  double growthMs,
  double populationMs,
  double trafficMs,
  double economyMs,
  double serviceMs,
  size_t buildingCount,
  uint32_t finalPopulation
) {
  const double totalMs = growthMs + populationMs + trafficMs + economyMs + serviceMs;
  const auto printPhaseTime = [](const char* label, double value, bool included) {
    std::cout << "    " << label << ": ";
    if (included) {
      std::cout << value << " ms\n";
    } else {
      std::cout << "excluded by focus\n";
    }
  };

  const bool includeGrowth = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Growth);
  const bool includePopulation = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Population);
  const bool includeTraffic = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Traffic);
  const bool includeEconomy = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Economy);
  const bool includeService = (focus == BenchmarkFocus::All || focus == BenchmarkFocus::Service);

  std::cout << "Phase 5 Performance Benchmark:\n";
  std::cout << "  Ticks: " << benchmarkTicks << "\n";
  std::cout << "  Focus: " << benchmarkFocusToString(focus) << "\n";
  std::cout << "  Final Buildings: " << buildingCount << "\n";
  std::cout << "  Final Population: " << finalPopulation << "\n";
  std::cout << "  Total Time: " << std::fixed << std::setprecision(2) << totalMs << " ms\n";
  std::cout << "  Avg/Tick: " << (benchmarkTicks > 0 ? (totalMs / benchmarkTicks) : 0.0) << " ms\n";
  std::cout << "  Breakdown:\n";
  printPhaseTime("Growth", growthMs, includeGrowth);
  printPhaseTime("Population", populationMs, includePopulation);
  printPhaseTime("Traffic", trafficMs, includeTraffic);
  printPhaseTime("Economy", economyMs, includeEconomy);
  printPhaseTime("Service", serviceMs, includeService);
}

int main(int argc, char* argv[]) {
  // Parse arguments
  int mapSize = 64;
  int numTicks = 100;
  uint32_t seed = 42;
  bool printMapFlag = false;
  int printTileX = -1, printTileY = -1;
  bool printZonesFlag = false;
  bool printDemandFlag = false;
  bool printGrowthSummaryFlag = false;
  bool printPopulationSummaryFlag = false;
  bool printPopulationGroupsFlag = false;
  bool printBuildingsFlag = false;
  bool printConnectivityMapFlag = false;
  bool runCommuteSimulationFlag = false;
  bool printTrafficSummaryFlag = false;
  bool runEconomyCalculationFlag = false;
  bool printBudgetSummaryFlag = false;
  bool runServiceEvaluationFlag = false;
  bool printServiceSummaryFlag = false;
  bool printCitySummaryFlag = false;
  std::string renderMapPath;
  int renderScale = 8;
  bool hasRenderView = false;
  int renderViewX = 0, renderViewY = 0, renderViewW = -1, renderViewH = -1;
  int verifyReplayGrowthSteps = -1;
  int benchmarkPhase5Ticks = -1;
  BenchmarkFocus benchmarkPhase5Focus = BenchmarkFocus::All;
  std::string saveCityPath;
  std::string loadCityPath;
  std::string inspectSnapshotPath;
  std::vector<std::tuple<std::string, int, int, int>> serviceRequests;
  int printTopEdgesCount = -1;
  bool hasTrafficOriginFilter = false;
  int trafficOriginX = -1, trafficOriginY = -1;
  bool hasTrafficDestinationFilter = false;
  int trafficDestinationX = -1, trafficDestinationY = -1;
  std::vector<std::tuple<int, int, int, int, std::string>> zoneRequests;  // x1, y1, x2, y2, type
  int runGrowthSteps = 0;
  int64_t districtPressurePool = -1;
  bool printGrowthPressureFlag = false;
  std::string exportGrowthPressurePath;
  std::string compareGrowthPressurePathA;
  std::string compareGrowthPressurePathB;
  std::string rankGrowthPressureBasePath;
  std::vector<std::string> rankGrowthPressureCandidatePaths;
  std::string runPolicySweepOutputDir;
  int sweepDistrictId = -1;
  std::vector<uint32_t> sweepSeeds;
  std::vector<int64_t> sweepCaps;
  std::vector<float> sweepAllocations;
  bool sweepManifestAllDistricts = false;
  int seedPopulation = -1;
  std::vector<std::tuple<int, int, int, int>> placeRoadRequests;  // x1, y1, x2, y2
  int findPathX1 = -1, findPathY1 = -1, findPathX2 = -1, findPathY2 = -1;
  bool listDistrictsFlag = false;
  int printDistrictSummaryId = -1;
  int printDistrictFacilitiesId = -1;
  int64_t printDistrictBalancingPool = -1;
  std::vector<std::tuple<std::string, int, int, int, int>> createDistrictRequests;  // name, x1, y1, x2, y2
  std::vector<std::tuple<int, std::string, float>> setDistrictTaxRequests;  // district_id, building_type, rate
  std::vector<std::tuple<int, float, float, float, float>> setDistrictServiceRequests;  // district_id, fire, police, health, edu
  std::vector<std::pair<int, float>> setDistrictAllocationRequests;  // district_id, allocation
  std::vector<std::pair<int, int64_t>> setDistrictBudgetCapRequests;  // district_id, cap
  std::vector<std::pair<int, int>> assignFacilityRequests;  // district_id, facility_id
  std::vector<std::pair<int, int>> unassignFacilityRequests;  // district_id, facility_id
  
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    
    if (arg == "--help") {
      printHelp();
      return 0;
    } else if (arg == "--size" && i + 1 < argc) {
      mapSize = std::atoi(argv[++i]);
    } else if (arg == "--ticks" && i + 1 < argc) {
      numTicks = std::atoi(argv[++i]);
    } else if (arg == "--seed" && i + 1 < argc) {
      seed = std::atoi(argv[++i]);
    } else if (arg == "--print-map") {
      printMapFlag = true;
    } else if (arg == "--print-tile" && i + 2 < argc) {
      printTileX = std::atoi(argv[++i]);
      printTileY = std::atoi(argv[++i]);
    } else if (arg == "--zone-rect" && i + 5 < argc) {
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      std::string type = argv[++i];
      zoneRequests.emplace_back(x1, y1, x2, y2, type);
    } else if (arg == "--print-zones") {
      printZonesFlag = true;
    } else if (arg == "--print-demand") {
      printDemandFlag = true;
    } else if (arg == "--print-growth-summary") {
      printGrowthSummaryFlag = true;
    } else if (arg == "--seed-population" && i + 1 < argc) {
      seedPopulation = std::atoi(argv[++i]);
    } else if (arg == "--print-population-summary") {
      printPopulationSummaryFlag = true;
    } else if (arg == "--print-population-groups") {
      printPopulationGroupsFlag = true;
    } else if (arg == "--run-growth" && i + 1 < argc) {
      runGrowthSteps = std::atoi(argv[++i]);
    } else if (arg == "--district-pressure-pool" && i + 1 < argc) {
      districtPressurePool = std::strtoll(argv[++i], nullptr, 10);
    } else if (arg == "--print-growth-pressure") {
      printGrowthPressureFlag = true;
    } else if (arg == "--export-growth-pressure" && i + 1 < argc) {
      exportGrowthPressurePath = argv[++i];
    } else if (arg == "--compare-growth-pressure" && i + 2 < argc) {
      compareGrowthPressurePathA = argv[++i];
      compareGrowthPressurePathB = argv[++i];
    } else if (arg == "--rank-growth-pressure" && i + 2 < argc) {
      const std::string basePath = argv[++i];
      const std::string candidatePath = argv[++i];
      if (rankGrowthPressureBasePath.empty()) {
        rankGrowthPressureBasePath = basePath;
      } else if (rankGrowthPressureBasePath != basePath) {
        std::cerr << "Error: --rank-growth-pressure must use the same BASE for all entries\n";
        return 1;
      }
      rankGrowthPressureCandidatePaths.push_back(candidatePath);
    } else if (arg == "--run-policy-sweep" && i + 1 < argc) {
      runPolicySweepOutputDir = argv[++i];
    } else if (arg == "--sweep-district" && i + 1 < argc) {
      sweepDistrictId = std::atoi(argv[++i]);
    } else if (arg == "--sweep-seeds" && i + 1 < argc) {
      if (!parseUint32List(argv[++i], sweepSeeds)) {
        std::cerr << "Error: --sweep-seeds expects a comma-separated list of integers\n";
        return 1;
      }
    } else if (arg == "--sweep-caps" && i + 1 < argc) {
      if (!parseInt64List(argv[++i], sweepCaps)) {
        std::cerr << "Error: --sweep-caps expects a comma-separated list of integers\n";
        return 1;
      }
    } else if (arg == "--sweep-allocations" && i + 1 < argc) {
      if (!parseFloatList(argv[++i], sweepAllocations)) {
        std::cerr << "Error: --sweep-allocations expects a comma-separated list of decimal values\n";
        return 1;
      }
    } else if (arg == "--sweep-manifest-all-districts") {
      sweepManifestAllDistricts = true;
    } else if (arg == "--print-buildings") {
      printBuildingsFlag = true;
    } else if (arg == "--connectivity-map") {
      printConnectivityMapFlag = true;
    } else if (arg == "--place-road" && i + 4 < argc) {
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      placeRoadRequests.emplace_back(x1, y1, x2, y2);
    } else if (arg == "--find-path" && i + 4 < argc) {
      findPathX1 = std::atoi(argv[++i]);
      findPathY1 = std::atoi(argv[++i]);
      findPathX2 = std::atoi(argv[++i]);
      findPathY2 = std::atoi(argv[++i]);
    } else if (arg == "--run-commute-simulation") {
      runCommuteSimulationFlag = true;
    } else if (arg == "--print-traffic-summary") {
      printTrafficSummaryFlag = true;
    } else if (arg == "--print-top-edges" && i + 1 < argc) {
      printTopEdgesCount = std::atoi(argv[++i]);
    } else if (arg == "--traffic-origin" && i + 2 < argc) {
      trafficOriginX = std::atoi(argv[++i]);
      trafficOriginY = std::atoi(argv[++i]);
      hasTrafficOriginFilter = true;
    } else if (arg == "--traffic-destination" && i + 2 < argc) {
      trafficDestinationX = std::atoi(argv[++i]);
      trafficDestinationY = std::atoi(argv[++i]);
      hasTrafficDestinationFilter = true;
    } else if (arg == "--create-district" && i + 5 < argc) {
      std::string name = argv[++i];
      int x1 = std::atoi(argv[++i]);
      int y1 = std::atoi(argv[++i]);
      int x2 = std::atoi(argv[++i]);
      int y2 = std::atoi(argv[++i]);
      createDistrictRequests.emplace_back(name, x1, y1, x2, y2);
    } else if (arg == "--list-districts") {
      listDistrictsFlag = true;
    } else if (arg == "--print-district-summary" && i + 1 < argc) {
      printDistrictSummaryId = std::atoi(argv[++i]);
    } else if (arg == "--print-district-facilities" && i + 1 < argc) {
      printDistrictFacilitiesId = std::atoi(argv[++i]);
    } else if (arg == "--print-district-balancing" && i + 1 < argc) {
      printDistrictBalancingPool = std::strtoll(argv[++i], nullptr, 10);
    } else if (arg == "--set-district-tax" && i + 3 < argc) {
      int districtId = std::atoi(argv[++i]);
      std::string buildingType = argv[++i];
      float rate = std::stof(argv[++i]);
      setDistrictTaxRequests.emplace_back(districtId, buildingType, rate);
    } else if (arg == "--set-district-service" && i + 5 < argc) {
      int districtId = std::atoi(argv[++i]);
      float fireWeight = std::stof(argv[++i]);
      float policeWeight = std::stof(argv[++i]);
      float healthWeight = std::stof(argv[++i]);
      float educationWeight = std::stof(argv[++i]);
      setDistrictServiceRequests.emplace_back(districtId, fireWeight, policeWeight, healthWeight, educationWeight);
    } else if (arg == "--set-district-allocation" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      float allocation = std::stof(argv[++i]);
      setDistrictAllocationRequests.emplace_back(districtId, allocation);
    } else if (arg == "--set-district-budget-cap" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int64_t cap = std::strtoll(argv[++i], nullptr, 10);
      setDistrictBudgetCapRequests.emplace_back(districtId, cap);
    } else if (arg == "--assign-facility" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int facilityId = std::atoi(argv[++i]);
      assignFacilityRequests.emplace_back(districtId, facilityId);
    } else if (arg == "--unassign-facility" && i + 2 < argc) {
      int districtId = std::atoi(argv[++i]);
      int facilityId = std::atoi(argv[++i]);
      unassignFacilityRequests.emplace_back(districtId, facilityId);
    } else if (arg == "--run-economy-calculation") {
      runEconomyCalculationFlag = true;
    } else if (arg == "--print-budget-summary") {
      printBudgetSummaryFlag = true;
    } else if (arg == "--add-service" && i + 4 < argc) {
      std::string serviceType = argv[++i];
      int x = std::atoi(argv[++i]);
      int y = std::atoi(argv[++i]);
      int dist = std::atoi(argv[++i]);
      serviceRequests.emplace_back(serviceType, x, y, dist);
    } else if (arg == "--run-service-evaluation") {
      runServiceEvaluationFlag = true;
    } else if (arg == "--print-service-summary") {
      printServiceSummaryFlag = true;
    } else if (arg == "--print-city-summary") {
      printCitySummaryFlag = true;
    } else if (arg == "--render-map" && i + 1 < argc) {
      renderMapPath = argv[++i];
    } else if (arg == "--render-scale" && i + 1 < argc) {
      renderScale = std::atoi(argv[++i]);
    } else if (arg == "--render-view" && i + 4 < argc) {
      renderViewX = std::atoi(argv[++i]);
      renderViewY = std::atoi(argv[++i]);
      renderViewW = std::atoi(argv[++i]);
      renderViewH = std::atoi(argv[++i]);
      hasRenderView = true;
    } else if (arg == "--save-city" && i + 1 < argc) {
      saveCityPath = argv[++i];
    } else if (arg == "--load-city" && i + 1 < argc) {
      loadCityPath = argv[++i];
    } else if (arg == "--inspect-snapshot" && i + 1 < argc) {
      inspectSnapshotPath = argv[++i];
    } else if (arg == "--benchmark-phase5" && i + 1 < argc) {
      benchmarkPhase5Ticks = std::atoi(argv[++i]);
    } else if (arg == "--benchmark-phase5-focus" && i + 1 < argc) {
      if (!parseBenchmarkFocus(argv[++i], benchmarkPhase5Focus)) {
        std::cerr << "Error: Unknown benchmark focus. Use ALL, GROWTH, POPULATION, TRAFFIC, ECONOMY, or SERVICE\n";
        return 1;
      }
    } else if (arg == "--verify-replay" && i + 1 < argc) {
      verifyReplayGrowthSteps = std::atoi(argv[++i]);
    }
  }
  
  // Validate arguments
  if (mapSize <= 0 || mapSize > 512) {
    std::cerr << "Error: Map size must be between 1 and 512\n";
    return 1;
  }
  if (numTicks < 0) {
    std::cerr << "Error: Number of ticks must be non-negative\n";
    return 1;
  }
  if (benchmarkPhase5Ticks < -1) {
    std::cerr << "Error: benchmark ticks must be non-negative\n";
    return 1;
  }
  if (districtPressurePool >= 0 && runGrowthSteps <= 0) {
    std::cerr << "Error: --district-pressure-pool requires --run-growth N\n";
    return 1;
  }
  if (printGrowthPressureFlag && districtPressurePool < 0) {
    std::cerr << "Error: --print-growth-pressure requires --district-pressure-pool POOL\n";
    return 1;
  }
  if (!exportGrowthPressurePath.empty() && districtPressurePool < 0) {
    std::cerr << "Error: --export-growth-pressure requires --district-pressure-pool POOL\n";
    return 1;
  }
  if (!exportGrowthPressurePath.empty() && runGrowthSteps <= 0) {
    std::cerr << "Error: --export-growth-pressure requires --run-growth N\n";
    return 1;
  }
  if ((!compareGrowthPressurePathA.empty() && compareGrowthPressurePathB.empty()) ||
      (compareGrowthPressurePathA.empty() && !compareGrowthPressurePathB.empty())) {
    std::cerr << "Error: --compare-growth-pressure requires two file paths\n";
    return 1;
  }
  if (!rankGrowthPressureBasePath.empty() && rankGrowthPressureCandidatePaths.empty()) {
    std::cerr << "Error: --rank-growth-pressure requires at least one candidate\n";
    return 1;
  }
  if (!runPolicySweepOutputDir.empty()) {
    if (runGrowthSteps <= 0) {
      std::cerr << "Error: --run-policy-sweep requires --run-growth N\n";
      return 1;
    }
    if (districtPressurePool < 0) {
      std::cerr << "Error: --run-policy-sweep requires --district-pressure-pool POOL\n";
      return 1;
    }
    if (sweepDistrictId <= 0) {
      std::cerr << "Error: --run-policy-sweep requires --sweep-district DIST_ID\n";
      return 1;
    }
    if (sweepSeeds.empty()) {
      std::cerr << "Error: --run-policy-sweep requires --sweep-seeds A,B,C\n";
      return 1;
    }
  }
    if ((hasTrafficOriginFilter || hasTrafficDestinationFilter) && printTopEdgesCount <= 0) {
      std::cerr << "Error: --traffic-origin/--traffic-destination require --print-top-edges N\n";
      return 1;
    }
  
  try {
    if (!rankGrowthPressureBasePath.empty()) {
      std::vector<GrowthPressureReportRow> baselineRows;
      std::string baselineError;
      if (!loadGrowthPressureReportCSV(rankGrowthPressureBasePath, baselineRows, baselineError)) {
        std::cerr << "Error: Failed to load baseline pressure report ('" << rankGrowthPressureBasePath
                  << "'): " << baselineError << "\n";
        return 1;
      }

      std::vector<std::pair<std::string, std::vector<GrowthPressureReportRow>>> candidates;
      candidates.reserve(rankGrowthPressureCandidatePaths.size());

      for (const std::string& candidatePath : rankGrowthPressureCandidatePaths) {
        std::vector<GrowthPressureReportRow> candidateRows;
        std::string candidateError;
        if (!loadGrowthPressureReportCSV(candidatePath, candidateRows, candidateError)) {
          std::cerr << "Error: Failed to load candidate pressure report ('" << candidatePath
                    << "'): " << candidateError << "\n";
          return 1;
        }
        candidates.emplace_back(candidatePath, std::move(candidateRows));
      }

      printGrowthPressureRanking(rankGrowthPressureBasePath, baselineRows, candidates);
      return 0;
    }

    if (!compareGrowthPressurePathA.empty()) {
      std::vector<GrowthPressureReportRow> rowsA;
      std::vector<GrowthPressureReportRow> rowsB;
      std::string errorA;
      std::string errorB;

      if (!loadGrowthPressureReportCSV(compareGrowthPressurePathA, rowsA, errorA)) {
        std::cerr << "Error: Failed to load pressure report A ('" << compareGrowthPressurePathA
                  << "'): " << errorA << "\n";
        return 1;
      }
      if (!loadGrowthPressureReportCSV(compareGrowthPressurePathB, rowsB, errorB)) {
        std::cerr << "Error: Failed to load pressure report B ('" << compareGrowthPressurePathB
                  << "'): " << errorB << "\n";
        return 1;
      }

      printGrowthPressureComparison(
        compareGrowthPressurePathA,
        rowsA,
        compareGrowthPressurePathB,
        rowsB
      );
      return 0;
    }

    if (!inspectSnapshotPath.empty()) {
      CitySnapshot inspectedSnapshot;
      SnapshotLoadDiagnostics inspectDiagnostics;
      if (!SaveLoadSystem::loadSnapshotFromFile(inspectSnapshotPath, inspectedSnapshot, &inspectDiagnostics)) {
        std::cerr << "Error: Failed to inspect city snapshot from '" << inspectSnapshotPath << "'\n";
        return 1;
      }
      printSnapshotInspection(inspectedSnapshot, inspectDiagnostics);
      return 0;
    }

    if (benchmarkPhase5Ticks >= 0) {
      std::cout << "Running Phase 5 benchmark on " << mapSize << "x" << mapSize
                << " map for " << benchmarkPhase5Ticks << " ticks"
                << " (focus=" << benchmarkFocusToString(benchmarkPhase5Focus) << ")...\n";

      CityMap benchmarkMap({mapSize, mapSize});
      RoadNetwork benchmarkRoads(benchmarkMap);
      EntityStore benchmarkStore;
      PopulationStore benchmarkPopulation;

      seedBenchmarkScenario(benchmarkMap, benchmarkRoads);

      std::vector<ServiceFacility> benchmarkFacilities;
      const int midX = mapSize / 2;
      const int midY = mapSize / 2;
      benchmarkFacilities.push_back(ServiceFacility{ServiceType::Fire, {midX - 6, midY}, 20, 1.0f});
      benchmarkFacilities.push_back(ServiceFacility{ServiceType::Police, {midX + 6, midY}, 20, 1.0f});
      benchmarkFacilities.push_back(ServiceFacility{ServiceType::Health, {midX, midY - 6}, 22, 1.0f});
      benchmarkFacilities.push_back(ServiceFacility{ServiceType::Education, {midX, midY + 6}, 24, 1.0f});

      double growthMs = 0.0;
      double populationMs = 0.0;
      double trafficMs = 0.0;
      double economyMs = 0.0;
      double serviceMs = 0.0;

      for (int tick = 0; tick < benchmarkPhase5Ticks; ++tick) {
        const uint32_t tickSeed = seed + static_cast<uint32_t>(tick * 13);
        const ZoneDemand demand = Zoning::calculateDemand(tickSeed);

        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Growth) {
          auto t0 = std::chrono::steady_clock::now();
          (void)GrowthSystem::runStep(benchmarkMap, benchmarkRoads, benchmarkStore, demand, tickSeed, 0.30f);
          auto t1 = std::chrono::steady_clock::now();
          growthMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)GrowthSystem::runStep(benchmarkMap, benchmarkRoads, benchmarkStore, demand, tickSeed, 0.30f);
        }

        const uint32_t requestedPopulation = static_cast<uint32_t>(std::max(1000, mapSize * mapSize / 2))
          + static_cast<uint32_t>((tick % 12) * 100);
        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Population) {
          auto t0 = std::chrono::steady_clock::now();
          (void)PopulationSystem::allocate(benchmarkStore, benchmarkPopulation, requestedPopulation, tickSeed + 1);
          auto t1 = std::chrono::steady_clock::now();
          populationMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)PopulationSystem::allocate(benchmarkStore, benchmarkPopulation, requestedPopulation, tickSeed + 1);
        }

        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Traffic) {
          auto t0 = std::chrono::steady_clock::now();
          (void)TrafficSystem::simulateCommutes(benchmarkStore, benchmarkPopulation, benchmarkRoads, tickSeed + 2);
          auto t1 = std::chrono::steady_clock::now();
          trafficMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)TrafficSystem::simulateCommutes(benchmarkStore, benchmarkPopulation, benchmarkRoads, tickSeed + 2);
        }

        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Economy) {
          auto t0 = std::chrono::steady_clock::now();
          (void)EconomySystem::calculateEconomy(benchmarkStore, benchmarkPopulation);
          auto t1 = std::chrono::steady_clock::now();
          economyMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)EconomySystem::calculateEconomy(benchmarkStore, benchmarkPopulation);
        }

        if (benchmarkPhase5Focus == BenchmarkFocus::All || benchmarkPhase5Focus == BenchmarkFocus::Service) {
          auto t0 = std::chrono::steady_clock::now();
          (void)ServiceSystem::evaluateCoverage(benchmarkStore, benchmarkRoads, benchmarkFacilities);
          auto t1 = std::chrono::steady_clock::now();
          serviceMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        } else {
          (void)ServiceSystem::evaluateCoverage(benchmarkStore, benchmarkRoads, benchmarkFacilities);
        }
      }

      printBenchmarkResults(
        benchmarkPhase5Ticks,
        benchmarkPhase5Focus,
        growthMs,
        populationMs,
        trafficMs,
        economyMs,
        serviceMs,
        benchmarkStore.getBuildingCount(),
        benchmarkPopulation.getTotalPopulation()
      );
      return 0;
    }

    CitySnapshot loadedSnapshot;
    SnapshotLoadDiagnostics snapshotDiagnostics;
    if (!loadCityPath.empty()) {
      if (!SaveLoadSystem::loadSnapshotFromFile(loadCityPath, loadedSnapshot, &snapshotDiagnostics)) {
        std::cerr << "Error: Failed to load city snapshot from '" << loadCityPath << "'\n";
        return 1;
      }
      std::cout << "Snapshot diagnostics: sourceVersion=" << snapshotDiagnostics.sourceVersion
                << ", targetVersion=" << snapshotDiagnostics.targetVersion
                << ", migrated=" << (snapshotDiagnostics.migrationApplied ? "yes" : "no")
                << ", path=" << snapshotDiagnostics.migrationPath
                << "\n";
      mapSize = loadedSnapshot.width;
    }

    if (verifyReplayGrowthSteps >= 0) {
      ReplayConfig config;
      config.mapSize = mapSize;
      config.seed = seed;
      config.growthSteps = verifyReplayGrowthSteps;
      config.seedPopulation = (seedPopulation >= 0) ? seedPopulation : 120;
      config.runCommutes = true;
      config.runEconomy = true;

      const ReplayResult replayResult = ReplayVerifier::verifyDeterministicRun(config);
      std::cout << "Replay Verification:\n";
      std::cout << "  First checksum:  " << replayResult.firstChecksum << "\n";
      std::cout << "  Second checksum: " << replayResult.secondChecksum << "\n";
      std::cout << "  Deterministic:   " << (replayResult.deterministic ? "Yes" : "No") << "\n";

      return replayResult.deterministic ? 0 : 1;
    }

    // Initialize city
    std::cout << "Initializing city (" << mapSize << "x" << mapSize << ")...\n";
    CityMap map({mapSize, mapSize});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    PopulationSummary populationSummary;
    TrafficSummary trafficSummary;
    EconomyState economyState;
    ServiceCoverageSummary serviceSummary;
    bool hasPopulationSummary = false;
    bool hasTrafficSummary = false;
    bool hasEconomyState = false;
    bool hasServiceSummary = false;
    bool districtMutationsApplied = false;

    std::vector<ServiceFacility> serviceFacilities;
    serviceFacilities.reserve(serviceRequests.size());

    auto saveIfRequested = [&]() -> bool {
      if (saveCityPath.empty()) {
        return true;
      }
      if (!SaveLoadSystem::saveToFile(saveCityPath, map, roads, store, population)) {
        std::cerr << "Error: Failed to save city snapshot to '" << saveCityPath << "'\n";
        return false;
      }
      std::cout << "City snapshot saved to " << saveCityPath << "\n";
      return true;
    };

    if (!loadCityPath.empty()) {
      if (!SaveLoadSystem::applySnapshot(loadedSnapshot, map, roads, store, population)) {
        std::cerr << "Error: Loaded snapshot dimensions do not match current city map\n";
        return 1;
      }
      populationSummary = buildPopulationSummaryFromState(store, population);
      hasPopulationSummary = true;
      std::cout << "Loaded city snapshot from " << loadCityPath << "\n";
    }

    for (const auto& [typeRaw, x, y, dist] : serviceRequests) {
      ServiceType type;
      if (!ServiceSystem::parseServiceType(typeRaw, type)) {
        std::cerr << "Error: Unknown service type '" << typeRaw
                  << "'. Use FIRE, POLICE, HEALTH/HOSPITAL, EDUCATION/SCHOOL.\n";
        return 1;
      }
      if (!map.isValid({x, y})) {
        std::cerr << "Error: Service location (" << x << "," << y << ") is out of bounds\n";
        return 1;
      }
      ServiceFacility facility;
      facility.type = type;
      facility.position = {x, y};
      facility.maxTravelDistance = std::max(0, dist);
      serviceFacilities.push_back(facility);
      std::cout << "Added service facility #" << serviceFacilities.size()
                << " (" << ServiceSystem::serviceTypeToString(type)
                << ") at (" << x << "," << y << ")"
                << " distance=" << facility.maxTravelDistance << "\n";
    }
    
    // Handle inspection commands
    if (printMapFlag) {
      printMap(map);
      if (!saveIfRequested()) return 1;
      return 0;
    }
    
    if (printTileX >= 0 && printTileY >= 0) {
      printTile(map, printTileX, printTileY);
      if (!saveIfRequested()) return 1;
      return 0;
    }

    // Apply district mutations before growth so growth-pressure uses current district state.
    if (!createDistrictRequests.empty()) {
      for (const auto& [name, x1, y1, x2, y2] : createDistrictRequests) {
        DistrictId districtId = DistrictSystem::createDistrict(
          name,
          {x1, y1},
          {x2, y2}
        );
        if (districtId == 0) {
          std::cerr << "Error: Failed to create district '" << name << "' (invalid bounds?)\n";
          return 1;
        }
        std::cout << "Created district '" << name << "' with ID " << districtId << "\n";
        std::cout << "  Bounds: (" << x1 << "," << y1 << ") to (" << x2 << "," << y2 << ")\n";
      }
      districtMutationsApplied = true;
      createDistrictRequests.clear();
    }

    if (!setDistrictTaxRequests.empty()) {
      for (const auto& [districtId, buildingType, rate] : setDistrictTaxRequests) {
        District* district = DistrictSystem::getDistrict(static_cast<DistrictId>(districtId));
        if (district == nullptr) {
          std::cerr << "Error: District " << districtId << " not found\n";
          return 1;
        }

        std::string typeUpper = buildingType;
        std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(),
                      [](unsigned char c) { return std::toupper(c); });

        bool success = false;
        if (typeUpper == "RESIDENTIAL") {
          district->taxRates.residentialRate = std::max(0.0f, std::min(1.0f, rate));
          success = true;
        } else if (typeUpper == "COMMERCIAL") {
          district->taxRates.commercialRate = std::max(0.0f, std::min(1.0f, rate));
          success = true;
        } else if (typeUpper == "INDUSTRIAL") {
          district->taxRates.industrialRate = std::max(0.0f, std::min(1.0f, rate));
          success = true;
        } else {
          std::cerr << "Error: Unknown building type '" << buildingType << "'\n";
          return 1;
        }

        if (success) {
          std::cout << "Set " << buildingType << " tax rate to " << std::fixed << std::setprecision(1)
                    << (rate * 100.0f) << "% for district " << districtId << "\n";
        }
      }
      districtMutationsApplied = true;
      setDistrictTaxRequests.clear();
    }

    if (!setDistrictServiceRequests.empty()) {
      for (const auto& [districtId, fireW, policeW, healthW, eduW] : setDistrictServiceRequests) {
        District* district = DistrictSystem::getDistrict(static_cast<DistrictId>(districtId));
        if (district == nullptr) {
          std::cerr << "Error: District " << districtId << " not found\n";
          return 1;
        }

        ServicePriority priorities;
        priorities.fireWeight = std::max(0.0f, fireW);
        priorities.policeWeight = std::max(0.0f, policeW);
        priorities.healthWeight = std::max(0.0f, healthW);
        priorities.educationWeight = std::max(0.0f, eduW);

        district->servicePriorities = priorities;
        std::cout << "Set service priorities for district " << districtId << ":\n";
        std::cout << "  Fire: " << std::fixed << std::setprecision(2) << fireW << "\n";
        std::cout << "  Police: " << std::fixed << std::setprecision(2) << policeW << "\n";
        std::cout << "  Health: " << std::fixed << std::setprecision(2) << healthW << "\n";
        std::cout << "  Education: " << std::fixed << std::setprecision(2) << eduW << "\n";
      }
      districtMutationsApplied = true;
      setDistrictServiceRequests.clear();
    }

    if (!setDistrictAllocationRequests.empty()) {
      for (const auto& [districtId, allocation] : setDistrictAllocationRequests) {
        bool success = DistrictSystem::setDistrictServiceAllocation(
          static_cast<DistrictId>(districtId),
          allocation
        );
        if (!success) {
          std::cerr << "Error: District " << districtId << " not found\n";
          return 1;
        }

        const District* district = DistrictSystem::getDistrictConst(static_cast<DistrictId>(districtId));
        std::cout << "Set service allocation for district " << districtId << " to "
                  << std::fixed << std::setprecision(1)
                  << ((district != nullptr ? district->serviceAllocation : allocation) * 100.0f)
                  << "%\n";
      }
      districtMutationsApplied = true;
      setDistrictAllocationRequests.clear();
    }

    if (!setDistrictBudgetCapRequests.empty()) {
      for (const auto& [districtId, cap] : setDistrictBudgetCapRequests) {
        bool success = DistrictSystem::setDistrictServiceBudgetCap(
          static_cast<DistrictId>(districtId),
          cap
        );
        if (!success) {
          std::cerr << "Error: District " << districtId << " not found\n";
          return 1;
        }

        if (cap < 0) {
          std::cout << "Disabled service budget cap for district " << districtId << "\n";
        } else {
          std::cout << "Set service budget cap for district " << districtId << " to " << cap << "\n";
        }
      }
      districtMutationsApplied = true;
      setDistrictBudgetCapRequests.clear();
    }

    if (!assignFacilityRequests.empty()) {
      for (const auto& [districtId, facilityId] : assignFacilityRequests) {
        bool success = DistrictSystem::assignFacilityToDistrict(
          static_cast<DistrictId>(districtId),
          static_cast<uint32_t>(facilityId)
        );
        if (!success) {
          std::cerr << "Error: Failed to assign facility " << facilityId << " to district " << districtId << "\n";
          return 1;
        }
        std::cout << "Assigned facility " << facilityId << " to district " << districtId << "\n";
      }
      districtMutationsApplied = true;
      assignFacilityRequests.clear();
    }

    if (!unassignFacilityRequests.empty()) {
      for (const auto& [districtId, facilityId] : unassignFacilityRequests) {
        bool success = DistrictSystem::unassignFacilityFromDistrict(
          static_cast<DistrictId>(districtId),
          static_cast<uint32_t>(facilityId)
        );
        if (!success) {
          std::cerr << "Error: Failed to unassign facility " << facilityId
                    << " from district " << districtId << "\n";
          return 1;
        }
        std::cout << "Unassigned facility " << facilityId << " from district " << districtId << "\n";
      }
      districtMutationsApplied = true;
      unassignFacilityRequests.clear();
    }

    if (!zoneRequests.empty()) {
      for (const auto& [x1, y1, x2, y2, zoneTypeRaw] : zoneRequests) {
        ZoneType zoneType = ZoneType::None;
        if (!Zoning::parseZoneType(zoneTypeRaw, zoneType)) {
          std::cerr << "Error: Unknown zone type '" << zoneTypeRaw
                    << "'. Use NONE, RESIDENTIAL, COMMERCIAL, INDUSTRIAL, or PARK.\n";
          return 1;
        }

        int zonedCount = 0;
        if (!Zoning::applyZoneRect(map, {x1, y1}, {x2, y2}, zoneType, &zonedCount)) {
          std::cerr << "Error: Zone rectangle is out of bounds\n";
          return 1;
        }

        std::cout << "Applied zone " << Zoning::zoneToString(static_cast<int>(zoneType))
                  << " to " << zonedCount << " tiles.\n";
      }
    }
    
    if (!placeRoadRequests.empty()) {
      for (const auto& [x1, y1, x2, y2] : placeRoadRequests) {
        if (x1 == x2 && y1 == y2) {
          continue;
        }

        if (x1 == x2 || y1 == y2) {
          const int dx = (x2 > x1) ? 1 : (x2 < x1 ? -1 : 0);
          const int dy = (y2 > y1) ? 1 : (y2 < y1 ? -1 : 0);

          int cx = x1;
          int cy = y1;
          while (cx != x2 || cy != y2) {
            const int nx = cx + dx;
            const int ny = cy + dy;
            roads.buildRoad({cx, cy}, {nx, ny});
            cx = nx;
            cy = ny;
          }
        } else {
          roads.buildRoad({x1, y1}, {x2, y2});
        }

        roads.updateConnectivity({x1, y1});
        std::cout << "Road built from (" << x1 << "," << y1
                  << ") to (" << x2 << "," << y2 << ")\n";
      }
      std::cout << "Total roads: " << roads.getRoadCount() << "\n";
    }

    if (!runPolicySweepOutputDir.empty()) {
      const District* baselineDistrict = DistrictSystem::getDistrictConst(static_cast<DistrictId>(sweepDistrictId));
      if (baselineDistrict == nullptr) {
        std::cerr << "Error: Sweep district " << sweepDistrictId << " not found\n";
        return 1;
      }

      const int64_t baselineCap = baselineDistrict->serviceBudgetCap;
      const float baselineAllocation = baselineDistrict->serviceAllocation;

      if (sweepCaps.empty()) {
        sweepCaps.push_back(baselineCap);
      }
      if (sweepAllocations.empty()) {
        sweepAllocations.push_back(baselineAllocation);
      }

      for (float& allocation : sweepAllocations) {
        allocation = std::max(0.0f, std::min(1.0f, allocation));
      }

      std::error_code fsError;
      std::filesystem::create_directories(runPolicySweepOutputDir, fsError);
      if (fsError) {
        std::cerr << "Error: Failed to create sweep output directory '" << runPolicySweepOutputDir
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

        if (!DistrictSystem::setDistrictServiceBudgetCap(static_cast<DistrictId>(sweepDistrictId), cap)) {
          throw std::runtime_error("failed to apply sweep service cap");
        }
        if (!DistrictSystem::setDistrictServiceAllocation(static_cast<DistrictId>(sweepDistrictId), allocation)) {
          throw std::runtime_error("failed to apply sweep service allocation");
        }

        SweepResult result;
        result.label = label;
        result.path = path;
        result.scenarioSeed = scenarioSeed;
        result.cap = cap;
        result.allocation = allocation;

        runGrowthStepsWithPressure(
          scenarioMap,
          scenarioRoads,
          scenarioStore,
          scenarioPopulation,
          serviceFacilities,
          runGrowthSteps,
          scenarioSeed,
          districtPressurePool,
          false,
          false,
          &result.rows
        );

        if (!writeGrowthPressureReportCSV(path, result.rows)) {
          throw std::runtime_error("failed to write growth pressure report");
        }

        return result;
      };

      std::cout << "Running policy sweep in " << runPolicySweepOutputDir << "\n";
      std::cout << "  District: " << sweepDistrictId << "\n";
      std::cout << "  Baseline: seed=" << seed
                << " cap=" << baselineCap
                << " allocation=" << std::fixed << std::setprecision(3) << baselineAllocation << "\n";

      const std::string baselinePath = (std::filesystem::path(runPolicySweepOutputDir) / "baseline.csv").string();
      SweepResult baselineResult;
      try {
        baselineResult = runSweepScenario(seed, baselineCap, baselineAllocation, "baseline", baselinePath);
      } catch (const std::exception& ex) {
        std::cerr << "Error: Policy sweep baseline failed: " << ex.what() << "\n";
        return 1;
      }
      std::cout << "  Wrote baseline report: " << baselinePath
                << " (rows=" << baselineResult.rows.size() << ")\n";

      std::vector<SweepResult> candidateResults;
      size_t scenarioOrdinal = 0;
      for (uint32_t scenarioSeed : sweepSeeds) {
        for (int64_t cap : sweepCaps) {
          for (float allocation : sweepAllocations) {
            if (scenarioSeed == seed && cap == baselineCap && std::abs(allocation - baselineAllocation) < 0.0001f) {
              continue;
            }

            ++scenarioOrdinal;
            std::ostringstream fileName;
            fileName << "scenario_" << scenarioOrdinal
                     << "_seed" << scenarioSeed
                     << "_cap" << cap
                     << "_alloc" << sanitizeToken(std::to_string(allocation))
                     << ".csv";
            const std::string reportPath = (std::filesystem::path(runPolicySweepOutputDir) / fileName.str()).string();

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
        printDistrictDeltaRanking(baselineResult.rows, rankingCandidates, 3);
      } else {
        std::cout << "Growth Pressure Ranking: no candidate scenarios generated beyond baseline\n";
      }

      const GrowthPressureSummary baselineSummary = summarizeGrowthPressureRows(baselineResult.rows);
      const GrowthPressureSummary baselineDistrictSummary = summarizeGrowthPressureRowsForDistrict(
        baselineResult.rows,
        static_cast<DistrictId>(sweepDistrictId)
      );

      const std::string manifestPath = (std::filesystem::path(runPolicySweepOutputDir) / "sweep_manifest.csv").string();
      std::ofstream manifest(manifestPath);
      if (!manifest.is_open()) {
        std::cerr << "Error: Failed to write sweep manifest to '" << manifestPath << "'\n";
        return 1;
      }

      manifest << "scenario,seed,district_id,cap,allocation,report_path,score,d_multiplier,d_fulfillment,d_cap_rate,d_alloc_share,district_samples,d_district_multiplier,d_district_fulfillment,d_district_cap_rate,d_district_alloc_share\n";
      manifest << "baseline," << seed << "," << sweepDistrictId << "," << baselineCap << ","
               << std::fixed << std::setprecision(6) << baselineAllocation << ","
               << csvEscape(baselinePath) << ",0.000000,0.000000,0.000000,0.000000,0.000000,"
               << baselineDistrictSummary.samples
               << ",0.000000,0.000000,0.000000,0.000000\n";

      for (const SweepResult& result : candidateResults) {
        const GrowthPressureSummary candidateSummary = summarizeGrowthPressureRows(result.rows);
        const GrowthPressureSummary candidateDistrictSummary = summarizeGrowthPressureRowsForDistrict(
          result.rows,
          static_cast<DistrictId>(sweepDistrictId)
        );

        const GrowthPressureDelta overallDelta = computeGrowthPressureDelta(baselineSummary, candidateSummary);
        const GrowthPressureDelta districtDelta = computeGrowthPressureDelta(baselineDistrictSummary, candidateDistrictSummary);
        const double score = scoreGrowthPressureSummaryDelta(baselineSummary, candidateSummary);

        manifest << csvEscape(result.label) << ","
                 << result.scenarioSeed << ","
                 << sweepDistrictId << ","
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

      if (sweepManifestAllDistricts) {
        const std::string districtManifestPath =
          (std::filesystem::path(runPolicySweepOutputDir) / "sweep_manifest_districts.csv").string();
        std::ofstream districtManifest(districtManifestPath);
        if (!districtManifest.is_open()) {
          std::cerr << "Error: Failed to write district sweep manifest to '" << districtManifestPath << "'\n";
          return 1;
        }

        districtManifest << "scenario,seed,district_id,district_name,cap,allocation,report_path,score,district_samples,d_district_multiplier,d_district_fulfillment,d_district_cap_rate,d_district_alloc_share\n";

        std::unordered_map<DistrictId, std::string> districtNames;
        for (const District& district : DistrictSystem::getDistricts()) {
          districtNames[district.id] = district.name;
        }

        const auto baselineByDistrict = summarizeGrowthPressureRowsByDistrict(baselineResult.rows);
        for (const auto& [districtId, summary] : baselineByDistrict) {
          std::string districtName = std::to_string(districtId);
          auto nameIt = districtNames.find(districtId);
          if (nameIt != districtNames.end()) {
            districtName = nameIt->second;
          }

          districtManifest << "baseline," << seed << "," << districtId << ","
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
    
    if (runGrowthSteps > 0) {
      std::vector<GrowthPressureReportRow> growthPressureRows;
      std::vector<GrowthPressureReportRow>* outRows = nullptr;
      if (!exportGrowthPressurePath.empty()) {
        outRows = &growthPressureRows;
      }

      runGrowthStepsWithPressure(
        map,
        roads,
        store,
        population,
        serviceFacilities,
        runGrowthSteps,
        seed,
        districtPressurePool,
        printGrowthPressureFlag,
        true,
        outRows
      );

      if (!exportGrowthPressurePath.empty()) {
        if (!writeGrowthPressureReportCSV(exportGrowthPressurePath, growthPressureRows)) {
          std::cerr << "Error: Failed to export growth pressure report to '"
                    << exportGrowthPressurePath << "'\n";
          return 1;
        }
        std::cout << "Exported growth pressure report to " << exportGrowthPressurePath
                  << " (rows=" << growthPressureRows.size() << ")\n";
      }
    }

    if (printDemandFlag) {
      printDemand(seed);
    }

    if (printConnectivityMapFlag) {
      roads.updateConnectivity({0, 0}); // Check connectivity from top-left
      printConnectivityMap(map, roads);
    }

    if (printZonesFlag) {
      printZones(map);
    }

    if (printBuildingsFlag) {
      printBuildings(store);
    }

    if (printGrowthSummaryFlag) {
      const GrowthMetrics growthMetrics = GrowthMetrics::collect(map, store);
      std::cout << growthMetrics.toString();
    }

    if (seedPopulation >= 0) {
      populationSummary = PopulationSystem::allocate(
        store,
        population,
        static_cast<uint32_t>(seedPopulation),
        seed
      );
      hasPopulationSummary = true;
    }

    if (printPopulationSummaryFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-population-summary requires --seed-population N\n";
        return 1;
      }
      printPopulationSummary(populationSummary);
    }

    if (printPopulationGroupsFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-population-groups requires --seed-population N\n";
        return 1;
      }
      printPopulationGroups(population);
    }

    if (runCommuteSimulationFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --run-commute-simulation requires --seed-population N\n";
        return 1;
      }
      trafficSummary = TrafficSystem::simulateCommutes(
        store, population, roads, seed
      );
      hasTrafficSummary = true;
      std::cout << "Commute simulation completed.\n";
    }

    if (printTrafficSummaryFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-traffic-summary requires --run-commute-simulation\n";
        return 1;
      }
      if (!hasTrafficSummary) {
        trafficSummary = TrafficSystem::simulateCommutes(
          store, population, roads, seed
        );
        hasTrafficSummary = true;
      }
      printTrafficSummary(trafficSummary);
    }

    if (printTopEdgesCount > 0) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-top-edges requires --seed-population N\n";
        return 1;
      }
      RouteDiagnosticsFilter routeFilter;
      if (hasTrafficOriginFilter) {
        if (!map.isValid({trafficOriginX, trafficOriginY})) {
          std::cerr << "Error: --traffic-origin coordinate is out of bounds\n";
          return 1;
        }
        routeFilter.hasOrigin = true;
        routeFilter.origin = {trafficOriginX, trafficOriginY};
      }
      if (hasTrafficDestinationFilter) {
        if (!map.isValid({trafficDestinationX, trafficDestinationY})) {
          std::cerr << "Error: --traffic-destination coordinate is out of bounds\n";
          return 1;
        }
        routeFilter.hasDestination = true;
        routeFilter.destination = {trafficDestinationX, trafficDestinationY};
      }

      printRouteDiagnosticsFilter(routeFilter);

      std::vector<EdgeTrafficData> topEdges;
      if (routeFilter.hasOrigin || routeFilter.hasDestination) {
        topEdges = TrafficSystem::getTopRouteDiagnosticEdges(
          store,
          population,
          roads,
          routeFilter,
          static_cast<size_t>(printTopEdgesCount),
          seed
        );
      } else {
        if (!hasTrafficSummary) {
          trafficSummary = TrafficSystem::simulateCommutes(
            store, population, roads, seed
          );
          hasTrafficSummary = true;
        }
        topEdges = TrafficSystem::getTopCongestedEdges(roads, printTopEdgesCount);
      }

      printTopCongestedEdges(topEdges);
    }

    if (runEconomyCalculationFlag) {
      economyState = EconomySystem::calculateEconomy(store, population);
      hasEconomyState = true;
      std::cout << "Economy calculation completed.\n";
    }

    if (printBudgetSummaryFlag) {
      if (!hasEconomyState) {
        economyState = EconomySystem::calculateEconomy(store, population);
        hasEconomyState = true;
      }
      printBudgetSummary(economyState);
    }

    if (runServiceEvaluationFlag) {
      serviceSummary = ServiceSystem::evaluateCoverage(store, roads, serviceFacilities);
      hasServiceSummary = true;
      std::cout << "Service evaluation completed.\n";
    }

    if (printServiceSummaryFlag) {
      if (!hasServiceSummary) {
        serviceSummary = ServiceSystem::evaluateCoverage(store, roads, serviceFacilities);
        hasServiceSummary = true;
      }
      printServiceSummary(serviceSummary);
    }

    if (printCitySummaryFlag) {
      if (!hasPopulationSummary) {
        std::cerr << "Error: --print-city-summary requires --seed-population N\n";
        return 1;
      }
      if (!hasTrafficSummary) {
        trafficSummary = TrafficSystem::simulateCommutes(
          store, population, roads, seed
        );
        hasTrafficSummary = true;
      }
      if (!hasEconomyState) {
        economyState = EconomySystem::calculateEconomy(store, population);
        hasEconomyState = true;
      }
      if (!hasServiceSummary && !serviceFacilities.empty()) {
        serviceSummary = ServiceSystem::evaluateCoverage(store, roads, serviceFacilities);
        hasServiceSummary = true;
      }

      CityMetrics summaryMetrics = MetricsSystem::collectCityMetrics(
        populationSummary,
        trafficSummary,
        economyState,
        hasServiceSummary ? &serviceSummary : nullptr
      );
      std::cout << MetricsSystem::createCitySummaryReport(summaryMetrics);
    }

    // Handle district commands
    if (!createDistrictRequests.empty()) {
      for (const auto& [name, x1, y1, x2, y2] : createDistrictRequests) {
        DistrictId districtId = DistrictSystem::createDistrict(
          name,
          {x1, y1},
          {x2, y2}
        );
        if (districtId == 0) {
          std::cerr << "Error: Failed to create district '" << name << "' (invalid bounds?)\n";
          return 1;
        }
        std::cout << "Created district '" << name << "' with ID " << districtId << "\n";
        std::cout << "  Bounds: (" << x1 << "," << y1 << ") to (" << x2 << "," << y2 << ")\n";
      }
    }

    // Handle district policy commands
    if (!setDistrictTaxRequests.empty()) {
      for (const auto& [districtId, buildingType, rate] : setDistrictTaxRequests) {
        District* district = DistrictSystem::getDistrict(static_cast<DistrictId>(districtId));
        if (district == nullptr) {
          std::cerr << "Error: District " << districtId << " not found\n";
          return 1;
        }
        
        std::string typeUpper = buildingType;
        std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(), 
                      [](unsigned char c) { return std::toupper(c); });
        
        bool success = false;
        if (typeUpper == "RESIDENTIAL") {
          district->taxRates.residentialRate = std::max(0.0f, std::min(1.0f, rate));
          success = true;
        } else if (typeUpper == "COMMERCIAL") {
          district->taxRates.commercialRate = std::max(0.0f, std::min(1.0f, rate));
          success = true;
        } else if (typeUpper == "INDUSTRIAL") {
          district->taxRates.industrialRate = std::max(0.0f, std::min(1.0f, rate));
          success = true;
        } else {
          std::cerr << "Error: Unknown building type '" << buildingType << "'\n";
          return 1;
        }
        
        if (success) {
          std::cout << "Set " << buildingType << " tax rate to " << std::fixed << std::setprecision(1)
                    << (rate * 100.0f) << "% for district " << districtId << "\n";
        }
      }
    }

    if (!setDistrictServiceRequests.empty()) {
      for (const auto& [districtId, fireW, policeW, healthW, eduW] : setDistrictServiceRequests) {
        District* district = DistrictSystem::getDistrict(static_cast<DistrictId>(districtId));
        if (district == nullptr) {
          std::cerr << "Error: District " << districtId << " not found\n";
          return 1;
        }
        
        ServicePriority priorities;
        priorities.fireWeight = std::max(0.0f, fireW);
        priorities.policeWeight = std::max(0.0f, policeW);
        priorities.healthWeight = std::max(0.0f, healthW);
        priorities.educationWeight = std::max(0.0f, eduW);
        
        district->servicePriorities = priorities;
        std::cout << "Set service priorities for district " << districtId << ":\n";
        std::cout << "  Fire: " << std::fixed << std::setprecision(2) << fireW << "\n";
        std::cout << "  Police: " << std::fixed << std::setprecision(2) << policeW << "\n";
        std::cout << "  Health: " << std::fixed << std::setprecision(2) << healthW << "\n";
        std::cout << "  Education: " << std::fixed << std::setprecision(2) << eduW << "\n";
      }
    }

    if (!setDistrictAllocationRequests.empty()) {
      for (const auto& [districtId, allocation] : setDistrictAllocationRequests) {
        bool success = DistrictSystem::setDistrictServiceAllocation(
          static_cast<DistrictId>(districtId),
          allocation
        );
        if (!success) {
          std::cerr << "Error: District " << districtId << " not found\n";
          return 1;
        }

        const District* district = DistrictSystem::getDistrictConst(static_cast<DistrictId>(districtId));
        std::cout << "Set service allocation for district " << districtId << " to "
                  << std::fixed << std::setprecision(1)
                  << ((district != nullptr ? district->serviceAllocation : allocation) * 100.0f)
                  << "%\n";
      }
    }

    if (!setDistrictBudgetCapRequests.empty()) {
      for (const auto& [districtId, cap] : setDistrictBudgetCapRequests) {
        bool success = DistrictSystem::setDistrictServiceBudgetCap(
          static_cast<DistrictId>(districtId),
          cap
        );
        if (!success) {
          std::cerr << "Error: District " << districtId << " not found\n";
          return 1;
        }

        if (cap < 0) {
          std::cout << "Disabled service budget cap for district " << districtId << "\n";
        } else {
          std::cout << "Set service budget cap for district " << districtId << " to " << cap << "\n";
        }
      }
    }

    if (!assignFacilityRequests.empty()) {
      for (const auto& [districtId, facilityId] : assignFacilityRequests) {
        bool success = DistrictSystem::assignFacilityToDistrict(
          static_cast<DistrictId>(districtId),
          static_cast<uint32_t>(facilityId)
        );
        if (!success) {
          std::cerr << "Error: Failed to assign facility " << facilityId << " to district " << districtId << "\n";
          return 1;
        }
        std::cout << "Assigned facility " << facilityId << " to district " << districtId << "\n";
      }
    }

    if (listDistrictsFlag) {
      const auto& districts = DistrictSystem::getDistricts();
      if (districts.empty()) {
        std::cout << "No districts created.\n";
      } else {
        std::cout << "Districts (" << districts.size() << "):\n";
        for (const auto& district : districts) {
          std::cout << "  [" << district.id << "] " << district.name << "\n";
          std::cout << "    Bounds: (" << district.minCorner.x << "," << district.minCorner.y
                    << ") to (" << district.maxCorner.x << "," << district.maxCorner.y << ")\n";
          std::cout << "    Area: " << district.area() << " tiles\n";
          std::cout << "    Tax Rates: Res=" << std::fixed << std::setprecision(2) 
                    << (district.taxRates.residentialRate * 100.0f) << "%  "
                    << "Com=" << (district.taxRates.commercialRate * 100.0f) << "%  "
                    << "Ind=" << (district.taxRates.industrialRate * 100.0f) << "%\n";
          std::cout << "    Service Allocation: " << (district.serviceAllocation * 100.0f) << "%\n";
          std::cout << "    Service Budget Cap: ";
          if (district.serviceBudgetCap < 0) {
            std::cout << "none\n";
          } else {
            std::cout << district.serviceBudgetCap << "\n";
          }
          std::cout << "    Service Priorities: Fire=" << district.servicePriorities.fireWeight
                    << " Police=" << district.servicePriorities.policeWeight
                    << " Health=" << district.servicePriorities.healthWeight
                    << " Education=" << district.servicePriorities.educationWeight << "\n";
          std::cout << "    Assigned Facilities: ";
          if (district.assignedFacilityIds.empty()) {
            std::cout << "none\n";
          } else {
            for (size_t idx = 0; idx < district.assignedFacilityIds.size(); ++idx) {
              if (idx > 0) {
                std::cout << ", ";
              }
              std::cout << district.assignedFacilityIds[idx];
            }
            std::cout << "\n";
          }
        }
      }
    }

    if (printDistrictSummaryId >= 0) {
      DistrictMetrics metrics = DistrictSystem::evaluateDistrictMetrics(
        static_cast<DistrictId>(printDistrictSummaryId),
        map, store, population,
        &roads,
        &serviceFacilities
      );
      
      if (metrics.districtId == 0) {
        std::cerr << "Error: District " << printDistrictSummaryId << " not found\n";
        return 1;
      }
      
      std::cout << "District Summary: " << metrics.districtName << "\n";
      std::cout << "  Population: " << metrics.population << "\n";
      std::cout << "  Buildings: " << metrics.buildings << " (Res: " << metrics.residentialBuildings
                << ", Com: " << metrics.commercialBuildings << ", Ind: " << metrics.industrialBuildings << ")\n";
      std::cout << "  Average Land Value: " << std::fixed << std::setprecision(1) << metrics.averageLandValue << "\n";
      std::cout << "  Budget: Revenue=" << metrics.revenue << ", Expenses=" << metrics.expenses
                << ", Balance=" << metrics.balance << "\n";
      std::cout << "  Service Budget: Target=" << metrics.serviceBudgetTarget
                << ", Allocated=" << metrics.serviceBudgetAllocated
                << ", CapApplied=" << (metrics.serviceBudgetCapApplied ? "yes" : "no") << "\n";
      std::cout << "  Service Coverage: " << std::fixed << std::setprecision(1) << (metrics.serviceCoverage * 100.0f) << "%\n";
      std::cout << "  Happiness: " << std::fixed << std::setprecision(1) << (metrics.happiness * 100.0f) << "%\n";
    }

    if (printDistrictBalancingPool >= 0) {
      const std::vector<DistrictMetrics> balancedMetrics = DistrictSystem::evaluateAllDistricts(
        map,
        store,
        population,
        &roads,
        &serviceFacilities,
        printDistrictBalancingPool
      );

      std::cout << "District Balancing (shared pool=" << printDistrictBalancingPool << "):\n";
      if (balancedMetrics.empty()) {
        std::cout << "  No districts created.\n";
      } else {
        int64_t totalAllocated = 0;
        for (const DistrictMetrics& metrics : balancedMetrics) {
          totalAllocated += metrics.serviceBudgetAllocated;
          std::cout << "  [" << metrics.districtId << "] " << metrics.districtName
                    << " target=" << metrics.serviceBudgetTarget
                    << " allocated=" << metrics.serviceBudgetAllocated
                    << " coverage=" << std::fixed << std::setprecision(1)
                    << (metrics.serviceCoverage * 100.0f) << "%"
                    << " capApplied=" << (metrics.serviceBudgetCapApplied ? "yes" : "no")
                    << "\n";
        }
        std::cout << "  Total Allocated: " << totalAllocated << "\n";
      }
    }

    if (printDistrictFacilitiesId >= 0) {
      const District* district = DistrictSystem::getDistrictConst(static_cast<DistrictId>(printDistrictFacilitiesId));
      if (district == nullptr) {
        std::cerr << "Error: District " << printDistrictFacilitiesId << " not found\n";
        return 1;
      }

      std::cout << "District Facilities: [" << district->id << "] " << district->name << "\n";
      if (district->assignedFacilityIds.empty()) {
        std::cout << "  none\n";
      } else {
        for (uint32_t id : district->assignedFacilityIds) {
          std::cout << "  " << id << "\n";
        }
      }
    }

    if (!renderMapPath.empty()) {
      RenderOptions renderOptions;
      renderOptions.tilePixels = std::max(1, renderScale);
      if (hasRenderView) {
        renderOptions.viewX = renderViewX;
        renderOptions.viewY = renderViewY;
        renderOptions.viewWidth = renderViewW;
        renderOptions.viewHeight = renderViewH;
      }

      if (!MapRenderer::renderToPPM(renderMapPath, map, store, renderOptions)) {
        std::cerr << "Error: Failed to render map to '" << renderMapPath << "'\n";
        return 1;
      }
      std::cout << "Rendered map image to " << renderMapPath << "\n";
    }

    if (findPathX1 >= 0) {
      Pathfinding::Path path = Pathfinding::findShortestPath(
        roads, {findPathX1, findPathY1}, {findPathX2, findPathY2}
      );
      printPath(path);
      if (!saveIfRequested()) return 1;
      return 0;
    }

    if (!zoneRequests.empty() || !placeRoadRequests.empty() || runGrowthSteps > 0 || printZonesFlag ||
        printDemandFlag || printConnectivityMapFlag || printBuildingsFlag ||
        printGrowthSummaryFlag || seedPopulation >= 0 || printPopulationSummaryFlag ||
        printPopulationGroupsFlag || runCommuteSimulationFlag || printTrafficSummaryFlag ||
        printTopEdgesCount > 0 || runEconomyCalculationFlag || printBudgetSummaryFlag ||
        runServiceEvaluationFlag || printServiceSummaryFlag || !serviceRequests.empty() ||
        printCitySummaryFlag || !renderMapPath.empty() || !saveCityPath.empty() || !loadCityPath.empty() ||
        !createDistrictRequests.empty() || listDistrictsFlag || printDistrictSummaryId >= 0 ||
        printDistrictFacilitiesId >= 0 ||
        printDistrictBalancingPool >= 0 ||
        !setDistrictTaxRequests.empty() || !setDistrictServiceRequests.empty() ||
        !setDistrictAllocationRequests.empty() || !setDistrictBudgetCapRequests.empty() ||
        !assignFacilityRequests.empty() || !unassignFacilityRequests.empty() || districtMutationsApplied) {
      if (!saveIfRequested()) return 1;
      return 0;
    }
    
    // Normal simulation
    SimulationTime time;
    time.ticksPerDay = 24;
    time.ticksPerMonth = 720;
    
    CityMetrics metrics;
    
    if (numTicks > 0) {
      std::cout << "Running " << numTicks << " ticks with seed " << seed << "...\n\n";
      
      // Simulation loop
      for (int tick = 0; tick < numTicks; ++tick) {
        // Print status at intervals
        if (tick % 24 == 0) {
          std::cout << "Tick " << tick 
                    << ": Time " << time.getCurrentHour() << "h "
                    << "(Day " << time.getCurrentDay() << ", Month " << time.getCurrentMonth() << ")\n";
        }
        
        // Advance time
        time.advance();
      }
    }
    
    std::cout << "\nSimulation complete.\n";
    std::cout << metrics.toString();

    if (!saveIfRequested()) {
      return 1;
    }
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
