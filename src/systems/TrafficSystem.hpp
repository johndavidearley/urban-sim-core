#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/metrics/CityMetrics.hpp"

struct TrafficSummary {
  uint32_t commutingPopulation = 0;
  float averageCommuteTime = 0.0f;
  float maxEdgeCongestion = 0.0f;
  float averageEdgeCongestion = 0.0f;
  uint32_t congestionDetectedEdges = 0;
  float totalCommuteBurden = 0.0f;
};

struct EdgeTrafficData {
  glm::ivec2 from;
  glm::ivec2 to;
  float congestion = 0.0f;
  float totalCommuters = 0.0f;
  float totalCommuteTime = 0.0f;
};

class TrafficSystem {
public:
  // Simulate commute flows for all employed population
  // Updates road network congestion, returns traffic summary
  static TrafficSummary simulateCommutes(
    EntityStore& store,
    PopulationStore& population,
    RoadNetwork& network,
    uint32_t seed = 0
  );

  // Apply traffic metrics to city metrics
  static void applyToMetrics(const TrafficSummary& summary, CityMetrics& metrics);

  // Get traffic data for visualization/reporting (sorted by congestion)
  static std::vector<EdgeTrafficData> getTopCongestedEdges(
    const RoadNetwork& network,
    size_t topN = 10
  );
};
