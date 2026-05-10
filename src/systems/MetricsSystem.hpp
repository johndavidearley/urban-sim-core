#pragma once

#include <string>

#include "src/metrics/CityMetrics.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/ServiceSystem.hpp"

class MetricsSystem {
public:
  // Compose a single city metrics snapshot from subsystem summaries.
  static CityMetrics collectCityMetrics(
    const PopulationSummary& population,
    const TrafficSummary& traffic,
    const EconomyState& economy,
    const ServiceCoverageSummary* serviceSummary = nullptr
  );

  // Generate a human-readable city report for CLI output.
  static std::string createCitySummaryReport(const CityMetrics& metrics);
};
