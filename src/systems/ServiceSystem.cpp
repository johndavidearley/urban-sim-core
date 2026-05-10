#include "ServiceSystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <queue>
#include <string>
#include <unordered_map>

namespace {
int typeIndex(ServiceType type) {
  return static_cast<int>(type);
}

std::string upper(const std::string& raw) {
  std::string normalized = raw;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return normalized;
}

bool hasRoadAdjacency(const RoadNetwork& roads, Coord coord) {
  const RoadNetwork::Node* node = roads.getNode(coord);
  return node != nullptr && !node->adjacent.empty();
}

bool resolveRoadAnchor(const RoadNetwork& roads, Coord coord, Coord& outAnchor) {
  if (hasRoadAdjacency(roads, coord)) {
    outAnchor = coord;
    return true;
  }

  const Coord neighbors[4] = {
    {coord.x + 1, coord.y},
    {coord.x - 1, coord.y},
    {coord.x, coord.y + 1},
    {coord.x, coord.y - 1}
  };

  for (const Coord& n : neighbors) {
    if (hasRoadAdjacency(roads, n)) {
      outAnchor = n;
      return true;
    }
  }

  return false;
}

int shortestRoadDistance(const RoadNetwork& roads, Coord start, Coord goal) {
  if (start == goal) {
    return 0;
  }

  std::queue<Coord> frontier;
  std::unordered_map<Coord, int, Vec2Hash> distance;

  frontier.push(start);
  distance[start] = 0;

  while (!frontier.empty()) {
    const Coord current = frontier.front();
    frontier.pop();

    const RoadNetwork::Node* node = roads.getNode(current);
    if (node == nullptr) {
      continue;
    }

    for (const RoadNodeId& neighborId : node->adjacent) {
      const Coord next = neighborId.coord;
      if (distance.find(next) != distance.end()) {
        continue;
      }

      const int nextDistance = distance[current] + 1;
      distance[next] = nextDistance;

      if (next == goal) {
        return nextDistance;
      }

      frontier.push(next);
    }
  }

  return -1;
}
} // namespace

bool ServiceSystem::parseServiceType(const std::string& raw, ServiceType& outType) {
  const std::string normalized = upper(raw);

  if (normalized == "FIRE") {
    outType = ServiceType::Fire;
    return true;
  }
  if (normalized == "POLICE") {
    outType = ServiceType::Police;
    return true;
  }
  if (normalized == "HEALTH" || normalized == "HOSPITAL") {
    outType = ServiceType::Health;
    return true;
  }
  if (normalized == "EDUCATION" || normalized == "SCHOOL") {
    outType = ServiceType::Education;
    return true;
  }

  return false;
}

const char* ServiceSystem::serviceTypeToString(ServiceType type) {
  switch (type) {
    case ServiceType::Fire:
      return "Fire";
    case ServiceType::Police:
      return "Police";
    case ServiceType::Health:
      return "Health";
    case ServiceType::Education:
      return "Education";
    default:
      return "Unknown";
  }
}

ServiceCoverageSummary ServiceSystem::evaluateCoverage(
  const EntityStore& store,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities
) {
  ServiceCoverageSummary summary;
  summary.totalBuildings = static_cast<uint32_t>(store.getBuildings().size());

  if (summary.totalBuildings == 0) {
    summary.satisfaction = 0.5f;
    return summary;
  }

  std::array<uint32_t, 4> coveredByType = {0, 0, 0, 0};

  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    Coord buildingAnchor;
    if (!resolveRoadAnchor(roads, building.position, buildingAnchor)) {
      continue;
    }

    bool anyCoverage = false;
    std::array<bool, 4> hasTypeCoverage = {false, false, false, false};

    for (const ServiceFacility& facility : facilities) {
      Coord facilityAnchor;
      if (!resolveRoadAnchor(roads, facility.position, facilityAnchor)) {
        continue;
      }

      const int distance = shortestRoadDistance(roads, buildingAnchor, facilityAnchor);
      if (distance < 0 || distance > facility.maxTravelDistance) {
        continue;
      }

      const int idx = typeIndex(facility.type);
      hasTypeCoverage[idx] = true;
      anyCoverage = true;
    }

    if (anyCoverage) {
      ++summary.servicedBuildings;
    }

    for (int i = 0; i < static_cast<int>(hasTypeCoverage.size()); ++i) {
      if (hasTypeCoverage[i]) {
        ++coveredByType[i];
      }
    }
  }

  const float denom = static_cast<float>(summary.totalBuildings);
  summary.fireCoverage = coveredByType[typeIndex(ServiceType::Fire)] / denom;
  summary.policeCoverage = coveredByType[typeIndex(ServiceType::Police)] / denom;
  summary.healthCoverage = coveredByType[typeIndex(ServiceType::Health)] / denom;
  summary.educationCoverage = coveredByType[typeIndex(ServiceType::Education)] / denom;

  summary.overallCoverage =
    (summary.fireCoverage + summary.policeCoverage + summary.healthCoverage + summary.educationCoverage) / 4.0f;

  summary.satisfaction = std::clamp(0.25f + (summary.overallCoverage * 0.75f), 0.0f, 1.0f);
  return summary;
}

void ServiceSystem::applyToMetrics(const ServiceCoverageSummary& summary, CityMetrics& metrics) {
  metrics.serviceCoverage = summary.overallCoverage;
  metrics.serviceSatisfaction = summary.satisfaction;

  const float happinessDelta = (summary.satisfaction - 0.5f) * 0.25f;
  metrics.happiness = std::clamp(metrics.happiness + happinessDelta, 0.0f, 1.0f);
}
