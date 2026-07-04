#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "src/core/ThreadPool.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/metrics/CityMetrics.hpp"
#include "src/networks/RoadNetwork.hpp"

enum class ServiceType : int {
  Fire = 0,
  Police = 1,
  Health = 2,
  Education = 3
};

struct ServiceFacility {
  ServiceType type = ServiceType::Fire;
  Coord position{0, 0};
  int maxTravelDistance = 10; // Road-edge hops
  float quality = 1.0f;
};

struct ServiceCoverageSummary {
  uint32_t totalBuildings = 0;
  uint32_t servicedBuildings = 0;

  float fireCoverage = 0.0f;
  float policeCoverage = 0.0f;
  float healthCoverage = 0.0f;
  float educationCoverage = 0.0f;

  float overallCoverage = 0.0f;
  float satisfaction = 0.5f;
};

// Pre-built BFS distance fields for all facilities. Rebuild only when the
// facility list changes; reuse across ticks to avoid redundant BFS work.
// The result cache (cachedResult / cachedBuildingCount) avoids re-evaluating
// building coverage when neither buildings nor facilities have changed.
struct ServiceCoverageCache {
  struct Entry {
    ServiceType type = ServiceType::Fire;
    std::unordered_map<Coord, int, Vec2Hash> distanceField;
  };
  std::vector<Entry> entries;
  size_t builtForFacilityCount = 0;

  // Nearest distance to *any* facility of *any* type, merged (min) across all
  // entries' distance fields once here in buildCache() rather than re-derived
  // by every caller that wants "distance to nearest facility of any type"
  // (e.g. LandValueSystem's service-proximity land value bonus) via an
  // O(entries) scan per query - a real cost at city scale, since that scan
  // used to run once per zoned, road-anchored tile every recompute.
  std::unordered_map<Coord, int, Vec2Hash> nearestAnyDistance;

  // Result cache: valid when cachedBuildingCount == store.getBuildings().size()
  // AND builtForFacilityCount matches. Reset to SIZE_MAX to force re-evaluation.
  size_t cachedBuildingCount = static_cast<size_t>(-1);
  ServiceCoverageSummary cachedResult;
};

class ServiceSystem {
public:
  static bool parseServiceType(const std::string& raw, ServiceType& outType);
  static const char* serviceTypeToString(ServiceType type);

  // Full evaluation: builds BFS fields then scores buildings. Use when no cache is available.
  static ServiceCoverageSummary evaluateCoverage(
    const EntityStore& store,
    const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities
  );

  // Rebuild the BFS distance fields (call only when facilities change).
  static void buildCache(
    const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities,
    ServiceCoverageCache& cache
  );

  // Score buildings against an already-built cache (cheap — no BFS, just lookups).
  // If pool is non-null, buildings are partitioned across pool workers.
  static ServiceCoverageSummary evaluateFromCache(
    const EntityStore& store,
    const RoadNetwork& roads,
    const ServiceCoverageCache& cache,
    ThreadPool* pool = nullptr
  );

  static void applyToMetrics(const ServiceCoverageSummary& summary, CityMetrics& metrics);
};
