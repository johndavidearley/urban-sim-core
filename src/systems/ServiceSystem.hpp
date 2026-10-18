#pragma once

#include <algorithm>
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
  Sanitation = 6,
  Garbage = 7,
  Recycling = 8,
  Cemetery = 9,
  Crematorium = 10,
  Incinerator = 11
};

constexpr size_t kServiceTypeCount = 12;

enum class PowerSourceType : int {
  Generic = 0,
  Coal = 1,
  NaturalGas = 2,
  Nuclear = 3,
  Solar = 4,
  Wind = 5,
  Hydro = 6
};

struct ServiceFacility {
  ServiceType type = ServiceType::Fire;
  Coord position{0, 0};
  int maxTravelDistance = 10; // Road-edge hops
  float quality = 1.0f;
  PowerSourceType powerSource = PowerSourceType::Generic;
  float powerCapacityMW = 100.0f;
  float emissionsKgPerMWh = 400.0f;
  // Water facilities only. A default plant covers the same small city as a
  // default 100 MW power plant (see utilityDemandFor).
  float waterSupplyUnits = 100.0f;
  // Water: shoreline intake. Sanitation: raw outlet or shoreline treatment.
  // False preserves legacy inland water / sanitation facilities.
  bool surfaceWater = false;
  int64_t landfillRemaining = 80000;
  int64_t landfillLastFill = 0; // net units deposited on the last completed tick
  bool sewageTreatment = false; // shoreline Sanitation only; no raw discharge

};

// Solar occupies a 2×2 whose minimum corner is `position`. Every other
// facility, including every other power source, occupies its anchor tile.
inline int facilityFootprintSpan(const ServiceFacility& facility) {
  return facility.type == ServiceType::Power &&
      facility.powerSource == PowerSourceType::Solar ? 2 : 1;
}

inline bool facilityCoversTile(const ServiceFacility& facility, Coord coord) {
  const int span = facilityFootprintSpan(facility);
  return coord.x >= facility.position.x && coord.y >= facility.position.y &&
    coord.x < facility.position.x + span && coord.y < facility.position.y + span;
}

// Occupancy load shared by power (MW) and water (units). Residential draws
// half as much as every other building type. A default 100 MW / 100-unit
// plant covers 50,000 residents before shedding starts.
inline float utilityDemandFor(const Building& building) {
  const float perOccupant =
    building.type == BuildingType::Residential ? 0.002f : 0.004f;
  return static_cast<float>(std::max(0, building.occupancy)) * perOccupant;
}

struct ServiceCoverageSummary {
  uint32_t totalBuildings = 0;
  uint32_t servicedBuildings = 0;

  float fireCoverage = 0.0f;
  float policeCoverage = 0.0f;
  float healthCoverage = 0.0f;
  float educationCoverage = 0.0f;
  float schoolSeats = 0.0f;
  float schoolDemand = 0.0f;
  float schoolSeatsUsed = 0.0f;
  float effectiveEducationCoverage = 0.0f;
  float hospitalBeds = 0.0f;
  float patientDemand = 0.0f;
  float hospitalBedsUsed = 0.0f;
  float hospitalCapacityFactor = 1.0f;

  // Utilities are tracked separately from overallCoverage/satisfaction
  // below (which stay exactly (fire+police+health+education)/4, unchanged)
  // rather than folded into that blend - utilities are opt-in
  // (SimOptions::enableUtilities), and every existing caller that doesn't
  // place utility facilities would otherwise see its overall coverage
  // silently drop once these two categories existed at all.
  float powerCoverage = 0.0f;
  float waterCoverage = 0.0f;
  float sanitationCoverage = 0.0f;
  float garbageCoverage = 0.0f;
  float recyclingCoverage = 0.0f;
  float cemeteryCoverage = 0.0f;
  float crematoriumCoverage = 0.0f;
  float incineratorCoverage = 0.0f;
  float powerDemandMW = 0.0f;
  float powerGenerationMW = 0.0f;
  float powerSupplyRatio = 1.0f;
  float powerEmissionsKgPerMWh = 0.0f;
  float waterDemand = 0.0f;
  float waterSupply = 0.0f;
  // 1 when demand is 0. Below 1 is a shortage the HUD and simulate summary
  // show; shedding (updateUtilityConnectivity) is what actually disconnects
  // buildings. The ratio stays demand/supply so a shed city still reads short.
  float waterSupplyRatio = 1.0f;

  float overallCoverage = 0.0f;
  float satisfaction = 0.5f;
};

// Bit pack of which ServiceType values cover a road node. kServiceTypeCount
// is 12, so a uint16_t is enough for one bit per type.
using ServiceCoverageMask = uint16_t;
static_assert(kServiceTypeCount <= 16, "ServiceCoverageMask needs more bits");

// Pre-built BFS distance fields for all facilities. Rebuild only when the
// facility list changes; reuse across ticks to avoid redundant BFS work.
// The result cache avoids re-evaluating building coverage when neither the
// entity store nor the topology-dependent distance fields have changed.
struct ServiceCoverageCache {
  struct Entry {
    ServiceType type = ServiceType::Fire;
    std::unordered_map<Coord, int, Vec2Hash> distanceField;
    int maxTravelDistance = 10;
  };
  std::vector<Entry> entries;
  // Funded waste facilities; fuel-dependent generation is evaluated from live occupancy.
  std::vector<ServiceFacility> wasteFacilities;
  size_t builtForFacilityCount = 0;
  uint64_t builtForFacilitySignature = 0;
  uint64_t builtForTopologyVersion = static_cast<uint64_t>(-1);
  float powerGenerationCapacityMW = 0.0f;
  float powerWeightedEmissions = 0.0f;
  float waterSupplyUnits = 0.0f;
  float schoolSeats = 0.0f;
  float hospitalBeds = 0.0f;

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

  // Per road-node bitset of covering service types, built once in buildCache.
  // evaluateFromCache does one map lookup per building instead of scanning
  // every facility distance field (O(buildings) vs O(buildings × facilities)).
  std::unordered_map<Coord, ServiceCoverageMask, Vec2Hash> coverageMask;

  // Result cache: valid when cachedStoreMutationVersion matches the entity
  // store. EntityStore bumps this on create/remove/clear/upsert, so a full
  // building-position rehash is unnecessary for structural edits. In-place
  // mutation through a non-const Building* without upsert does not invalidate
  // — callers that edit buildings in place should use upsertBuilding.
  uint64_t cachedStoreMutationVersion = static_cast<uint64_t>(-1);
  ServiceCoverageSummary cachedResult;
};

class ServiceSystem {
public:
  // Local response at an already-resolved road anchor; reads cached BFS fields.
  static float coverageStrength(const ServiceCoverageCache& cache, ServiceType type, Coord anchor);

  static bool parseServiceType(const std::string& raw, ServiceType& outType);
  static const char* serviceTypeToString(ServiceType type, bool surfaceWater = false, bool sewageTreatment = false);
  // Bounded raw-discharge exposure along connected water tiles (12 hops).
  static float intakeCleanFraction(const RoadNetwork& roads,
      const std::vector<ServiceFacility>& facilities, Coord intake);
  static bool parsePowerSourceType(const std::string& raw, PowerSourceType& outType);
  static const char* powerSourceTypeToString(PowerSourceType type);
  static float defaultPowerEmissions(PowerSourceType type);

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
  static void refreshWasteStorage(const std::vector<ServiceFacility>& facilities,
    ServiceCoverageCache& cache);

  static bool isCacheValid(
    const RoadNetwork& roads,
    const std::vector<ServiceFacility>& facilities,
    const ServiceCoverageCache& cache
  );

  // True when the entity store has not been structurally mutated since the
  // last storeCachedResult (O(1) mutation-version compare).
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

  // Illness rate is a severity index, about 0.5 even under full care. A fifth
  // of it needs a bed, so one 400-bed hospital serves about 3,000 residents.
  static constexpr float kPatientShare = 0.2f;
  static void applyCivicCapacity(ServiceCoverageSummary& summary, uint32_t housedPopulation, float illnessRate);

  static void applyToMetrics(const ServiceCoverageSummary& summary, CityMetrics& metrics);
};
