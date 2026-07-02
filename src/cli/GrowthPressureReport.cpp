#include "src/cli/GrowthPressureReport.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_set>

#include "src/cli/CliUtils.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/world/Zoning.hpp"

namespace {
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
} // namespace

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
  const DistrictSystem& districtSystem,
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
  for (const District& district : districtSystem.getDistricts()) {
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
) {
  if (districtPressurePool >= 0 && printStepOutput) {
    std::cout << "Applying district growth pressure with shared pool "
              << districtPressurePool << "\n";
  }

  if (outRows != nullptr) {
    outRows->clear();
    outRows->reserve(static_cast<size_t>(runGrowthSteps) * std::max<size_t>(1, districtSystem.getDistricts().size()));
  }

  for (int step = 0; step < runGrowthSteps; ++step) {
    const ZoneDemand demand = Zoning::calculateDemand(baseSeed + static_cast<uint32_t>(step));

    std::vector<GrowthChanceModifier> growthModifiers;
    if (districtPressurePool >= 0) {
      const auto balancedMetrics = districtSystem.evaluateAllDistricts(
        map,
        store,
        population,
        &roads,
        &serviceFacilities,
        districtPressurePool
      );

      std::unordered_map<DistrictId, float> districtChanceMultiplier;
      for (const DistrictMetrics& metrics : balancedMetrics) {
        const District* district = districtSystem.getDistrictConst(metrics.districtId);
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

      const auto& districts = districtSystem.getDistricts();
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
                << ", I=" << stats.spawnedIndustrial
                << ", O=" << stats.spawnedOffice << ")"
                << " demolished=" << stats.totalDemolished()
                << " (R=" << stats.demolishedResidential
                << ", C=" << stats.demolishedCommercial
                << ", I=" << stats.demolishedIndustrial
                << ", O=" << stats.demolishedOffice << ")\n";
    }
  }
}
