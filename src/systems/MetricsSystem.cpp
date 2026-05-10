#include "MetricsSystem.hpp"

#include <iomanip>
#include <sstream>

CityMetrics MetricsSystem::collectCityMetrics(
  const PopulationSummary& population,
  const TrafficSummary& traffic,
  const EconomyState& economy
) {
  CityMetrics metrics;
  PopulationSystem::applyToMetrics(population, metrics);
  TrafficSystem::applyToMetrics(traffic, metrics);
  EconomySystem::applyToMetrics(economy, metrics);
  return metrics;
}

std::string MetricsSystem::createCitySummaryReport(const CityMetrics& metrics) {
  std::stringstream ss;
  ss << "City Summary:\n";
  ss << metrics.toString();

  const int64_t netBalance = metrics.cityRevenue - metrics.cityExpenses;
  ss << std::fixed << std::setprecision(1);
  ss << "Budget Balance: $" << netBalance << "\n";

  if (netBalance >= 0) {
    ss << "Budget Status: Surplus\n";
  } else {
    ss << "Budget Status: Deficit\n";
  }

  return ss.str();
}
