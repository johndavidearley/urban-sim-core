#include "src/metrics/CityMetrics.hpp"
#include <sstream>
#include <iomanip>

std::string CityMetrics::toString() const {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(1);
  ss << "Population: " << population << "\n";
  ss << "Available Housing: " << availableHousing << "\n";
  ss << "Available Jobs: " << availableJobs << "\n";
  ss << "Unemployment: " << (unemployment * 100.0f) << "%\n";
  ss << "Happiness: " << (happiness * 100.0f) << "%\n";
  ss << "Pollution: " << (pollution * 100.0f) << "%\n";
  ss << "Land Value (avg): $" << landValueAverage << "\n";
  ss << "City Revenue: $" << cityRevenue << "\n";
  ss << "City Expenses: $" << cityExpenses << "\n";
  ss << "Commute Burden: " << (commuteBurden * 100.0f) << "%\n";
  ss << "Traffic Congestion: " << (trafficCongestion * 100.0f) << "%\n";
  return ss.str();
}
