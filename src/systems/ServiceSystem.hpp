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
  Education = 3,
  Power = 4,
  Water = 5,
  Sanitation = 6
};

constexpr size_t kServiceTypeCount = 7;

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

  // Utilities are tracked separately from overallCoverage/satisfaction
  // below (which stay exactly (fire+police+health+education)/4, unchanged)
  // rather than folded into that blend - utilities are opt-in
  // (SimOptions::enableUtilities), and every existing caller that doesn't
  // place utility facilities would otherwise see its overall coverage
  // silently drop once these two categories existed at all.
  float powerCoverage = 0.0f;
  float waterCoverage = 0.0f;
  float sanitationCoverage = 0.0f;

  float overallCoverage = 0.0f;
  float satisfaction = 0.5f;
};

// Pre-built BFS distance fields for all facilities. Rebuild only when the
// facility list changes; reuse across ticks to avoid redundant BFS work.
// The result cache avoids re-evaluating building coverage when neither the
// entity store nor the topology-dependent distance fields have changed.
struct ServiceCoverageCache {
  struct Entry {
    ServiceType type = ServiceType::Fire;
    std::unordered_map<Coord, int, Vec2Hash> distanceField;
  };
  std::vector<Entry> entries;
  size_t builtForFacilityCount = 0;
  uint64_t builtForFacilitySignature = 0;
  uint64_t builtForTopologyVersion = static_cast<uint64_t>(-1);

  // Nearest distance to *any* facility of *any* type, merged (min) across all
  // entries' distance fields once here in buildCache() rather than re-derived
  // by every caller that wants "distance to nearest facility of any type"
  // (e.g. LandValueSystem's service-proximity land value bonus) via an
  // O(entries) scan per query - a real cost at city scale, since that scan
  // used to run once per zoned, road-anchored tile every recompute.
  std::unordered_map<Coord, int, Vec2Hash> nearestAnyDistance;

  // Same merge as nearestAnyDistance, but restricted to Power/Water entries
  // respectively - lets a per-tile utility-connectivity check (e.g.
  // CitySimulator's Tile::connectedToPower/connectedToWater update, gated
  // behind SimOptions::enableUtilities) be a single map lookup instead of
  // an O(entries) scan filtered by type, once per tile, every tick.
  std::unordered_map<Coord, int, Vec2Hash> nearestPowerDistance;
  std::unordered_map<Coord, int, Vec2Hash> nearestWaterDistance;

  // Result cache: valid when cachedStoreMutationVersion matches the entity
  // store and the distance fields remain valid. A count alone is insufficient:
  // demolition followed by construction can change positions without changing
  // the number of buildings.
  uint64_t cachedStoreMutationVersion = static_cast<uint64_t>(-1);
  uint64_t cachedBuildingCoverageSignature = 0;
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

  // True when the topology and every coverage-relevant facility property
  // still match the state used by buildCache().
  static bool isCacheValid(
    const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities,
    const ServiceCoverageCache& cache
  );

  // Result reuse additionally checks building IDs and positions. This catches
  // coverage-relevant edits made through EntityStore's mutable Building API,
  // which do not advance the store's structural mutation version.
  static bool isResultCacheValid(
    const EntityStore& store,
    const ServiceCoverageCache& cache
  );

  static void storeCachedResult(
    const EntityStore& store,
    const ServiceCoverageSummary& result,
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
