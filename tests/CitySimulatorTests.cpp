#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

#include "src/systems/CitySimulator.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"

namespace {
SimOptions fastOptions() {
  SimOptions options;
  options.runTraffic = false;  // keep tests fast; traffic is exercised elsewhere
  return options;
}
} // namespace

// An empty city wants residences first (startup floor), no shops/industry yet.
TEST(CitySimulatorTests, EmptyCityDemandsResidentialFirst) {
  EntityStore store;
  PopulationStore population;

  const ZoneDemand demand = CitySimulator::evaluateDemand(store, population);

  EXPECT_GT(demand.residential, 0.5f);
  EXPECT_FLOAT_EQ(demand.commercial, 0.0f);
  EXPECT_FLOAT_EQ(demand.industrial, 0.0f);
}

// Residents with no jobs => unemployment kills residential demand and drives
// strong commercial/industrial demand.
TEST(CitySimulatorTests, UnemployedResidentsDemandJobs) {
  EntityStore store;
  PopulationStore population;

  for (int i = 0; i < 10; ++i) {
    store.createBuilding(BuildingType::Residential, {i, 0}, 8);  // resCap = 80
  }
  population.createGroup(IncomeBand::Middle, 80, 0);  // 80 residents, none employed

  const ZoneDemand demand = CitySimulator::evaluateDemand(store, population);

  EXPECT_LT(demand.residential, 0.1f);
  EXPECT_GT(demand.commercial, 0.5f);
  EXPECT_GT(demand.industrial, 0.5f);
}

// From a blank map the simulation must bootstrap a real, mixed city.
TEST(CitySimulatorTests, GrowsAMixedCityFromEmpty) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 40, fastOptions());

  ASSERT_FALSE(result.rows.empty());
  const SimTickMetrics& last = result.rows.back();
  EXPECT_GT(last.population, 100u);
  EXPECT_GT(last.residentialBuildings, 0u);
  EXPECT_GT(last.commercialBuildings, 0u);
  EXPECT_GT(last.industrialBuildings, 0u);
  EXPECT_GT(last.roadTiles, 0u);
  EXPECT_GT(last.employed, 0u);

  // Population should trend upward over the run, not collapse.
  EXPECT_GT(last.population, result.rows.front().population);
}

TEST(CitySimulatorTests, SameSeedProducesIdenticalCity) {
  auto runOnce = [](uint32_t seed) {
    CityMap map({40, 40});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    return CitySimulator::run(map, roads, store, population, seed, 30, fastOptions()).rows.back();
  };

  const SimTickMetrics a = runOnce(123);
  const SimTickMetrics b = runOnce(123);

  EXPECT_EQ(a.population, b.population);
  EXPECT_EQ(a.residentialBuildings, b.residentialBuildings);
  EXPECT_EQ(a.commercialBuildings, b.commercialBuildings);
  EXPECT_EQ(a.industrialBuildings, b.industrialBuildings);
  EXPECT_EQ(a.roadTiles, b.roadTiles);
}

// Residents migrate in gradually, so newly built housing is not instantly full
// (which the old instant-fill behavior would produce).
TEST(CitySimulatorTests, PopulationMigratesGraduallyNotInstantFill) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 5, fastOptions());

  ASSERT_FALSE(result.rows.empty());
  // Default residential capacity is 8 per building; early on the population
  // should sit well below the housing it has built (vacancy from migration lag).
  const SimTickMetrics& first = result.rows.front();
  ASSERT_GT(first.residentialBuildings, 0u);
  EXPECT_LT(first.population, first.residentialBuildings * 8u);
}

// The city should provision public services as it grows, giving real coverage.
TEST(CitySimulatorTests, ProvidesServicesAsItGrows) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 50, fastOptions());

  ASSERT_FALSE(result.rows.empty());
  const SimTickMetrics& last = result.rows.back();
  EXPECT_GT(last.serviceFacilities, 0u);
  EXPECT_GT(last.serviceCoverage, 0.0f);
}

// Pollution-aware zoning should push industry into dirtier areas than housing.
TEST(CitySimulatorTests, IndustryAndHousingSegregateByPollution) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  CitySimulator::run(map, roads, store, population, 7, 50, fastOptions());

  double residentialPollution = 0.0;
  double industrialPollution = 0.0;
  uint32_t residentialCount = 0;
  uint32_t industrialCount = 0;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    const float p = map.getTile(building.position).pollution;
    if (building.type == BuildingType::Residential) {
      residentialPollution += p;
      ++residentialCount;
    } else if (building.type == BuildingType::Industrial) {
      industrialPollution += p;
      ++industrialCount;
    }
  }

  ASSERT_GT(residentialCount, 0u);
  ASSERT_GT(industrialCount, 0u);
  EXPECT_LT(residentialPollution / residentialCount, industrialPollution / industrialCount);
}

TEST(CitySimulatorTests, NeverBuildsOnWater) {
  CityMap map({48, 48});
  TerrainParams params;
  params.waterFraction = 0.25f;
  TerrainGenerator::generate(map, 5, params);

  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  CitySimulator::run(map, roads, store, population, 5, 40, fastOptions());

  // No building may occupy a water tile.
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    EXPECT_NE(map.getTile(building.position).type, 2)
        << "building on water at (" << building.position.x << "," << building.position.y << ")";
  }
}

// A grown city should have a real, engaged (not flat-default) land value
// field: variation across zoned tiles, and the reported per-tick average
// tracking whatever LandValueSystem computes on the final map state.
TEST(CitySimulatorTests, LandValueVariesAcrossZonedTiles) {
  CityMap map({48, 48});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 40, fastOptions());

  ASSERT_FALSE(result.rows.empty());
  EXPECT_GT(result.rows.back().avgLandValue, 0.0f);

  float minValue = std::numeric_limits<float>::max();
  float maxValue = std::numeric_limits<float>::lowest();
  const glm::ivec2 dims = map.getDimensions();
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      if (tile.type == 2 || tile.zone == static_cast<int>(ZoneType::None)) continue;
      minValue = std::min(minValue, tile.landValue);
      maxValue = std::max(maxValue, tile.landValue);
    }
  }
  EXPECT_GT(maxValue, minValue) << "land value should vary spatially, not be a flat constant";
}

// Default options (inflationRatePerTick == 0) must leave the reported
// inflation multiplier pinned at 1.0 for every tick - the zero-behavior-change
// guarantee for existing callers that don't opt in.
TEST(CitySimulatorTests, NoInflationByDefault) {
  CityMap map({40, 40});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 30, fastOptions());

  ASSERT_FALSE(result.rows.empty());
  for (const SimTickMetrics& row : result.rows) {
    EXPECT_FLOAT_EQ(row.inflationMultiplier, 1.0f);
  }
}

// A nonzero inflation rate compounds the reported multiplier upward over
// ticks, and - because tax revenue doesn't inflate while maintenance/trade
// costs do - a growing but inflation-pressured city ends up worse off than
// the same city with no inflation at all.
TEST(CitySimulatorTests, InflationCompoundsAndErodesBalanceRelativeToNoInflation) {
  auto runWithInflation = [](float rate) {
    CityMap map({40, 40});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    SimOptions options = fastOptions();
    options.inflationRatePerTick = rate;
    return CitySimulator::run(map, roads, store, population, 7, 30, options);
  };

  const SimResult withInflation = runWithInflation(0.05f);
  const SimResult withoutInflation = runWithInflation(0.0f);

  ASSERT_FALSE(withInflation.rows.empty());
  ASSERT_FALSE(withoutInflation.rows.empty());

  const SimTickMetrics& lastInflated = withInflation.rows.back();
  const SimTickMetrics& lastFlat = withoutInflation.rows.back();

  EXPECT_GT(lastInflated.inflationMultiplier, 1.0f);
  EXPECT_GT(lastInflated.inflationMultiplier, withInflation.rows.front().inflationMultiplier);

  // Same seed/growth trajectory (inflation doesn't touch demand or growth),
  // so building counts should match; only the budget should diverge.
  EXPECT_EQ(lastInflated.residentialBuildings, lastFlat.residentialBuildings);
  EXPECT_EQ(lastInflated.commercialBuildings, lastFlat.commercialBuildings);
  EXPECT_EQ(lastInflated.industrialBuildings, lastFlat.industrialBuildings);
  EXPECT_LT(lastInflated.budgetBalance, lastFlat.budgetBalance);
}

// An empty city (no commercial base yet) should generate no office demand -
// office space follows an established retail/downtown base, it doesn't lead it.
TEST(CitySimulatorTests, EmptyCityGeneratesNoOfficeDemand) {
  EntityStore store;
  PopulationStore population;

  const ZoneDemand demand = CitySimulator::evaluateDemand(store, population);

  EXPECT_FLOAT_EQ(demand.office, 0.0f);
}

// Once commercial capacity is established and population exists, office
// demand should turn on.
TEST(CitySimulatorTests, OfficeDemandRampsUpOnceCommercialIsEstablished) {
  EntityStore store;
  PopulationStore population;

  for (int i = 0; i < 10; ++i) {
    store.createBuilding(BuildingType::Residential, {i, 0}, 8);  // resCap = 80
  }
  for (int i = 0; i < 5; ++i) {
    store.createBuilding(BuildingType::Commercial, {i, 1}, 20);  // comCap = 100
  }
  population.createGroup(IncomeBand::Middle, 60, 50);

  const ZoneDemand demand = CitySimulator::evaluateDemand(store, population);

  EXPECT_GT(demand.office, 0.0f);
  EXPECT_LE(demand.office, 1.0f);
}

// A grown city should include genuine Office buildings among its RCI mix,
// with demand.office reported per tick and used to zone/build real
// BuildingType::Office structures - not just a cosmetic demand number.
TEST(CitySimulatorTests, GrowsOfficeBuildingsAlongsideRCI) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 60, fastOptions());

  ASSERT_FALSE(result.rows.empty());
  const SimTickMetrics& last = result.rows.back();
  EXPECT_GT(last.officeBuildings, 0u);

  uint32_t officeCountInStore = 0;
  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    if (building.type == BuildingType::Office) {
      ++officeCountInStore;
    }
  }
  EXPECT_EQ(officeCountInStore, last.officeBuildings);
}
