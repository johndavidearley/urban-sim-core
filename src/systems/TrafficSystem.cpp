#include "TrafficSystem.hpp"
#include "src/networks/Pathfinding.hpp"
#include <algorithm>
#include <random>
#include <numeric>

TrafficSummary TrafficSystem::simulateCommutes(
  EntityStore& store,
  PopulationStore& population,
  RoadNetwork& network,
  uint32_t seed
) {
  TrafficSummary summary;

  // Reset network congestion for this tick
  network.resetCongestion();

  // Get all population groups
  const auto& groups = population.getGroups();
  uint32_t totalCommuters = 0;
  float totalCommuteTime = 0.0f;

  // Seeded RNG for deterministic job assignment
  std::mt19937 rng(seed);

  // Get all buildings by type
  const auto& buildings = store.getBuildings();
  std::vector<EntityId> residentialBuildings;
  std::vector<EntityId> jobBuildings; // Commercial + Industrial

  for (const auto& [id, building] : buildings) {
    if (building.type == BuildingType::Residential) {
      residentialBuildings.push_back(id);
    } else if (building.type == BuildingType::Commercial || 
               building.type == BuildingType::Industrial) {
      jobBuildings.push_back(id);
    }
  }

  // If no job buildings, no commutes possible
  if (jobBuildings.empty() || residentialBuildings.empty()) {
    summary.commutingPopulation = 0;
    summary.averageCommuteTime = 0.0f;
    return summary;
  }

  // For each population group, assign workers to jobs and simulate commutes
  for (const auto& [groupId, group] : groups) {
    // Only people who are employed commute
    if (group.employed == 0) {
      continue;
    }

    totalCommuters += group.employed;

    // Distribute workers across commutes
    // Each employed person takes a commute path
    uint32_t workersPerCommute = std::max(1u, group.employed / 10u);
    uint32_t numCommutes = (group.employed + workersPerCommute - 1) / workersPerCommute;

    for (uint32_t c = 0; c < numCommutes; ++c) {
      // Select source residential building (deterministically from seed)
      std::uniform_int_distribution<size_t> residentialDist(0, residentialBuildings.size() - 1);
      EntityId residentialId = residentialBuildings[residentialDist(rng)];
      const Building& residentialBldg = buildings.at(residentialId);

      // Select destination job building (deterministically from seed)
      std::uniform_int_distribution<size_t> jobDist(0, jobBuildings.size() - 1);
      EntityId jobId = jobBuildings[jobDist(rng)];
      const Building& jobBldg = buildings.at(jobId);

      // Calculate commute path using pathfinding with congestion
      auto path = Pathfinding::findShortestPathWithCongestion(
        network,
        residentialBldg.position,
        jobBldg.position
      );

      if (path.found && path.waypoints.size() > 1) {
        // Accumulate traffic on path edges
        for (size_t i = 0; i < path.waypoints.size() - 1; ++i) {
          glm::ivec2 from = path.waypoints[i];
          glm::ivec2 to = path.waypoints[i + 1];

          // Update network edge with commuter load
          network.updateCongestion(from, to, static_cast<float>(workersPerCommute));
        }

        // Add commute time (scaled by actual commuters in this batch)
        uint32_t commuters = std::min(workersPerCommute, group.employed - (c * workersPerCommute));
        totalCommuteTime += path.totalDistance * commuters;
      }
    }
  }

  // Calculate summary metrics
  summary.commutingPopulation = totalCommuters;

  if (totalCommuters > 0) {
    summary.averageCommuteTime = totalCommuteTime / static_cast<float>(totalCommuters);
    summary.totalCommuteBurden = totalCommuteTime;
  }

  // Calculate edge congestion statistics
  float maxCongestion = 0.0f;
  float totalCongestion = 0.0f;
  uint32_t congestionDetected = 0;

  auto allTraffic = network.getAllEdgeTraffic();
  for (const auto& traffic : allTraffic) {
    float congestion = traffic.congestion;
    if (congestion > 0.0f) {
      congestionDetected++;
      totalCongestion += congestion;
      maxCongestion = std::max(maxCongestion, congestion);
    }
  }

  summary.maxEdgeCongestion = maxCongestion;
  summary.averageEdgeCongestion = congestionDetected > 0 ? 
    totalCongestion / static_cast<float>(congestionDetected) : 0.0f;
  summary.congestionDetectedEdges = congestionDetected;

  return summary;
}

void TrafficSystem::applyToMetrics(const TrafficSummary& summary, CityMetrics& metrics) {
  // Commute burden as a 0-1 scale based on average commute time
  // Longer commutes reduce happiness/increase burden
  // Reference: 5 units = 0.5 burden, 10 units = 0.8 burden
  float commuteScale = summary.averageCommuteTime / 5.0f;
  metrics.commuteBurden = std::min(1.0f, commuteScale * 0.1f);

  // Traffic congestion directly maps to network congestion
  metrics.trafficCongestion = summary.maxEdgeCongestion;

  // Reduce happiness based on commute burden and traffic
  float happinessPenalty = metrics.commuteBurden * 0.2f + metrics.trafficCongestion * 0.15f;
  metrics.happiness = std::max(0.0f, metrics.happiness - happinessPenalty);
}

std::vector<EdgeTrafficData> TrafficSystem::getTopCongestedEdges(
  const RoadNetwork& network,
  size_t topN
) {
  std::vector<EdgeTrafficData> edges;

  auto allTraffic = network.getAllEdgeTraffic();
  for (const auto& traffic : allTraffic) {
    if (traffic.congestion > 0.0f) {
      EdgeTrafficData data;
      data.from = traffic.from;
      data.to = traffic.to;
      data.congestion = traffic.congestion;
      data.totalCommuters = traffic.currentLoad;
      edges.push_back(data);
    }
  }

  // Sort by congestion descending
  std::sort(edges.begin(), edges.end(), 
    [](const EdgeTrafficData& a, const EdgeTrafficData& b) {
      return a.congestion > b.congestion;
    });

  // Return top N
  if (edges.size() > topN) {
    edges.resize(topN);
  }

  return edges;
}
