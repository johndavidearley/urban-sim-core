#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TransitSystem.hpp"
#include "src/world/Zoning.hpp"

class CityMap;
class EntityStore;
class PopulationStore;
class RoadNetwork;

struct GameplaySessionState {
  int64_t funds = 50000;
  uint32_t tick = 0;
  bool paused = false;
  uint32_t tickIntervalMs = 350;
  ZoneDemand demand;
  int64_t treasuryRevenue = 0;
  int64_t treasuryExpenses = 0;
  int64_t treasuryNet = 0;
  int64_t treasuryDebt = 0;
  double treasuryInterestRemainder = 0.0;
  uint32_t populationTarget = 480;
  double fractionalDeaths = 0.0;
  uint32_t awaitingDisposition = 0;
  std::vector<ServiceFacility> facilities;
  // Visualizer G-mode: CitySimulator-style autonomous road/zone expansion.
  bool autonomousGrowth = false;
  int autonomousExtent = 0;
  int64_t emptyZonedCount = 0;
  std::vector<TransitRoute> transitRoutes;
  // Policy districts painted in the visualizer. Older sessions omit this
  // array and load as an empty set.
  struct DistrictRecord {
    std::string name;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    int archetype = 0;
    int64_t serviceBudgetCap = -1;  // negative = uncapped
  };
  std::vector<DistrictRecord> districts;
};

class GameplaySessionSystem {
public:
  static bool readMapDimensions(
    const std::string& sessionPath,
    int& width,
    int& height,
    std::string* errorMessage = nullptr
  );

  static bool save(
    const std::string& sessionPath,
    const CityMap& map,
    const RoadNetwork& roads,
    const EntityStore& store,
    const PopulationStore& population,
    const GameplaySessionState& session,
    std::string* errorMessage = nullptr
  );

  static bool load(
    const std::string& sessionPath,
    CityMap& map,
    RoadNetwork& roads,
    EntityStore& store,
    PopulationStore& population,
    GameplaySessionState& session,
    std::string* errorMessage = nullptr
  );
};
