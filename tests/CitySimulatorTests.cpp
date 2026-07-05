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

// A long run on a large map forces many road-grid expansions (the extent
// grows in fixed steps as demand persists), repeatedly exercising the
// zoning candidate index's incremental ring update rather than just its
// initial full scan. Zoning must keep pace with demand throughout - a bug
// in the ring decomposition (e.g. missing a band, or an off-by-one in the
// inner-box buffer) would show up as growth stalling out partway through
// once the initially-scanned region's candidates run dry.
TEST(CitySimulatorTests, ZoningKeepsPaceThroughManyRoadGridExpansions) {
  CityMap map({200, 200});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 11, 250, fastOptions());

  ASSERT_FALSE(result.rows.empty());

  // Population (and therefore zoned/built land) should keep climbing all the
  // way to the end, not plateau early - a stalled candidate index would
  // starve growth well before the run finishes.
  const SimTickMetrics& firstHalf = result.rows[result.rows.size() / 2];
  const SimTickMetrics& last = result.rows.back();
  EXPECT_GT(last.population, firstHalf.population);
  EXPECT_GT(last.residentialBuildings, 0u);
  EXPECT_GT(last.commercialBuildings, 0u);
  EXPECT_GT(last.industrialBuildings, 0u);
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
    const float p = map.pollution(building.position);
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
      const float landValue = map.landValue({x, y});
      minValue = std::min(minValue, landValue);
      maxValue = std::max(maxValue, landValue);
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

// A city grown with traffic (and therefore transit, on by default) running
// long enough to cross the population threshold should end up with at least
// one auto-placed bus route, and carry real ridership on at least some ticks.
// Commute pairs are drawn uniformly at random each tick (TrafficSystem's
// existing model, not something transit changes), so ridership is inherently
// noisy tick-to-tick - checking "any row" rather than specifically the last
// row is the correct, non-flaky way to assert the mechanism actually fires.
TEST(CitySimulatorTests, TransitRoutesAppearAndCarryRidershipAsCityGrows) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 120, SimOptions{});

  ASSERT_FALSE(result.rows.empty());
  const SimTickMetrics& last = result.rows.back();
  EXPECT_GT(last.transitRoutes, 0u);

  uint32_t totalRidership = 0;
  for (const SimTickMetrics& row : result.rows) {
    totalRidership += row.transitRidership;
    EXPECT_GE(row.transitModalShare, 0.0f);
    EXPECT_LE(row.transitModalShare, 1.0f);
  }
  EXPECT_GT(totalRidership, 0u);
}

// options.enableTransit = false must behave exactly like before this feature
// existed: no routes ever placed, zero ridership reported every tick.
TEST(CitySimulatorTests, TransitDisabledMeansNoRoutesEverPlaced) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  SimOptions options;
  options.enableTransit = false;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 70, options);

  ASSERT_FALSE(result.rows.empty());
  for (const SimTickMetrics& row : result.rows) {
    EXPECT_EQ(row.transitRoutes, 0u);
    EXPECT_EQ(row.transitRidership, 0u);
    EXPECT_FLOAT_EQ(row.transitModalShare, 0.0f);
  }
}

// A city grown large enough (population past the rail threshold) should get
// at least one rail line alongside its bus routes - a genuine second transit
// mode, not just relabeled buses. bus + rail route counts must always sum to
// the total, on every row that has any routes at all.
TEST(CitySimulatorTests, RailLinesAppearAlongsideBusesInALargeCity) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 180, SimOptions{});

  ASSERT_FALSE(result.rows.empty());
  const SimTickMetrics& last = result.rows.back();
  EXPECT_GT(last.transitBusRoutes, 0u);
  EXPECT_GT(last.transitRailRoutes, 0u);

  for (const SimTickMetrics& row : result.rows) {
    EXPECT_EQ(row.transitBusRoutes + row.transitRailRoutes, row.transitRoutes);
  }
}

// Passing an empty (but non-null) DistrictSystem must behave identically to
// the default nullptr - the growth-chance-modifier code path only activates
// once at least one district exists.
TEST(CitySimulatorTests, EmptyDistrictSystemMatchesNoDistrictsBehavior) {
  auto runOnce = [](const DistrictSystem* districts) {
    CityMap map({48, 48});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    return CitySimulator::run(map, roads, store, population, 7, 40, fastOptions(), districts).rows.back();
  };

  const DistrictSystem empty;
  const SimTickMetrics withNullptr = runOnce(nullptr);
  const SimTickMetrics withEmptyDistricts = runOnce(&empty);

  EXPECT_EQ(withNullptr.residentialBuildings, withEmptyDistricts.residentialBuildings);
  EXPECT_EQ(withNullptr.commercialBuildings, withEmptyDistricts.commercialBuildings);
  EXPECT_EQ(withNullptr.industrialBuildings, withEmptyDistricts.industrialBuildings);
  EXPECT_EQ(withNullptr.population, withEmptyDistricts.population);
}

// A district with a severely capped service budget should grow visibly
// slower (fewer buildings for the same area) than a same-sized, similarly
// central district with no cap - proving computeGrowthPressureMultiplier's
// output genuinely throttles GrowthSystem, not just sits in DistrictMetrics
// as an inert number. The two districts are mirrored across the city center
// (equal size, equal distance from the point growth radiates from) so the
// only meaningful difference between them is budget policy.
TEST(CitySimulatorTests, CappedServiceBudgetDistrictGrowsSlowerThanFundedOne) {
  CityMap map({80, 80});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  DistrictSystem districts;
  const DistrictId funded = districts.createDistrict("Funded", {10, 30}, {38, 50});
  const DistrictId capped = districts.createDistrict("Capped", {42, 30}, {70, 50});
  ASSERT_NE(funded, 0u);
  ASSERT_NE(capped, 0u);
  ASSERT_TRUE(districts.setDistrictServiceBudgetCap(funded, -1));   // uncapped
  ASSERT_TRUE(districts.setDistrictServiceBudgetCap(capped, 1));    // effectively starved

  SimOptions options = fastOptions();
  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 60, options, &districts);

  ASSERT_EQ(result.finalDistrictMetrics.size(), 2u);
  const DistrictMetrics& fundedMetrics = result.finalDistrictMetrics[0].districtId == funded
    ? result.finalDistrictMetrics[0] : result.finalDistrictMetrics[1];
  const DistrictMetrics& cappedMetrics = result.finalDistrictMetrics[0].districtId == capped
    ? result.finalDistrictMetrics[0] : result.finalDistrictMetrics[1];

  EXPECT_TRUE(cappedMetrics.serviceBudgetCapApplied);
  EXPECT_FALSE(fundedMetrics.serviceBudgetCapApplied);
  EXPECT_GT(fundedMetrics.buildings, cappedMetrics.buildings);
}

// A district with an Industrial archetype (bans Residential/Office) must end
// up with genuinely zero residential/office buildings within its bounds,
// even though it's positioned to contain the city's growth origin (the
// hardest case: autoZone's fallback-to-allowed-type logic has to keep the
// whole city's bootstrap alive despite its own preferred type being banned
// right where growth starts).
TEST(CitySimulatorTests, IndustrialArchetypeDistrictHasNoResidentialOrOfficeBuildings) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  DistrictSystem districts;
  const DistrictId industrialZone = districts.createDistrict("IndustrialZone", {20, 20}, {44, 44});
  ASSERT_NE(industrialZone, 0u);
  ASSERT_TRUE(districts.setDistrictArchetype(industrialZone, DistrictArchetype::Industrial));

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 60, fastOptions(), &districts);

  ASSERT_EQ(result.finalDistrictMetrics.size(), 1u);
  const DistrictMetrics& metrics = result.finalDistrictMetrics[0];
  EXPECT_EQ(metrics.residentialBuildings, 0u);
  EXPECT_EQ(metrics.officeBuildings, 0u);

  // The city as a whole must not deadlock just because its center district
  // bans the type growth wants most early on - land there should still
  // develop as something (Commercial/Industrial), and the city overall
  // should keep growing via other means once roads reach unrestricted land.
  EXPECT_GT(metrics.buildings, 0u);
  ASSERT_FALSE(result.rows.empty());
  EXPECT_GT(result.rows.back().population, 0u);
}

// A district with a TechHub archetype (bans Industrial) must end up with
// genuinely zero industrial buildings within its bounds.
TEST(CitySimulatorTests, TechHubArchetypeDistrictHasNoIndustrialBuildings) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  DistrictSystem districts;
  const DistrictId techHub = districts.createDistrict("TechHub", {20, 20}, {44, 44});
  ASSERT_NE(techHub, 0u);
  ASSERT_TRUE(districts.setDistrictArchetype(techHub, DistrictArchetype::TechHub));

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 60, fastOptions(), &districts);

  ASSERT_EQ(result.finalDistrictMetrics.size(), 1u);
  EXPECT_EQ(result.finalDistrictMetrics[0].industrialBuildings, 0u);
  EXPECT_GT(result.finalDistrictMetrics[0].buildings, 0u);
}

// options.enableDisasters defaults to false: fires must never occur, exactly
// matching pre-M15 behavior for every existing caller.
TEST(CitySimulatorTests, DisastersDisabledByDefaultMeansNoFiresEver) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 80, SimOptions{});

  ASSERT_FALSE(result.rows.empty());
  for (const SimTickMetrics& row : result.rows) {
    EXPECT_EQ(row.activeFires, 0u);
    EXPECT_EQ(row.buildingsLostToFire, 0u);
  }
}

// With disasters enabled and an elevated fire risk, a city growing over many
// ticks should end up with real cumulative fire losses - proving the
// integration actually destroys buildings within the live simulation, not
// just in FireSystem's own unit tests.
TEST(CitySimulatorTests, ElevatedFireRiskDestroysBuildingsOverTime) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  SimOptions options = fastOptions();
  options.enableDisasters = true;
  options.fireParams.baseIgnitionChance = 0.02f;  // far above the default, for a fast, reliable test

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 60, options);

  ASSERT_FALSE(result.rows.empty());
  EXPECT_GT(result.rows.back().buildingsLostToFire, 0u);

  // Cumulative losses must be monotonically non-decreasing tick to tick.
  uint32_t previous = 0;
  for (const SimTickMetrics& row : result.rows) {
    EXPECT_GE(row.buildingsLostToFire, previous);
    previous = row.buildingsLostToFire;
  }
}

// Crime is a pure read-out that runs every tick unconditionally (unlike the
// opt-in disasters above), so a normally growing city should report a
// nonzero, bounded crime rate throughout the run.
TEST(CitySimulatorTests, CrimeRateIsReportedEveryTickWithinBounds) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 60, fastOptions());

  ASSERT_FALSE(result.rows.empty());
  bool sawNonzeroCrime = false;
  for (const SimTickMetrics& row : result.rows) {
    EXPECT_GE(row.crimeRate, 0.0f);
    EXPECT_LE(row.crimeRate, 1.0f);
    if (row.crimeRate > 0.0f) sawNonzeroCrime = true;
  }
  EXPECT_TRUE(sawNonzeroCrime);
}

// A harsher crime model (higher base rate, weaker police mitigation) should
// visibly raise the reported crime rate relative to the default, proving
// options.crimeParams actually reaches CrimeSystem within the live loop.
TEST(CitySimulatorTests, HarsherCrimeParamsRaiseReportedCrimeRate) {
  auto runWithParams = [](const CrimeParams& params) {
    CityMap map({64, 64});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    SimOptions options = fastOptions();
    options.crimeParams = params;
    return CitySimulator::run(map, roads, store, population, 7, 60, options).rows.back();
  };

  CrimeParams mild;
  mild.baseCrimeRate = 0.05f;
  mild.policeCoverageReduction = 0.9f;

  CrimeParams harsh;
  harsh.baseCrimeRate = 0.5f;
  harsh.policeCoverageReduction = 0.1f;

  const SimTickMetrics mildLast = runWithParams(mild);
  const SimTickMetrics harshLast = runWithParams(harsh);

  EXPECT_GT(harshLast.crimeRate, mildLast.crimeRate);
}

// Health is a pure read-out that runs every tick unconditionally, exactly
// like crime above, so a normally growing city should report a nonzero,
// bounded illness rate throughout the run.
TEST(CitySimulatorTests, IllnessRateIsReportedEveryTickWithinBounds) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 60, fastOptions());

  ASSERT_FALSE(result.rows.empty());
  bool sawNonzeroIllness = false;
  for (const SimTickMetrics& row : result.rows) {
    EXPECT_GE(row.illnessRate, 0.0f);
    EXPECT_LE(row.illnessRate, 1.0f);
    if (row.illnessRate > 0.0f) sawNonzeroIllness = true;
  }
  EXPECT_TRUE(sawNonzeroIllness);
}

// A harsher health model (higher base rate, weaker hospital mitigation)
// should visibly raise the reported illness rate relative to the default,
// proving options.healthParams actually reaches HealthSystem within the
// live loop.
TEST(CitySimulatorTests, HarsherHealthParamsRaiseReportedIllnessRate) {
  auto runWithParams = [](const HealthParams& params) {
    CityMap map({64, 64});
    RoadNetwork roads(map);
    EntityStore store;
    PopulationStore population;
    SimOptions options = fastOptions();
    options.healthParams = params;
    return CitySimulator::run(map, roads, store, population, 7, 60, options).rows.back();
  };

  HealthParams mild;
  mild.baseIllnessRate = 0.02f;
  mild.healthCoverageReduction = 0.95f;

  HealthParams harsh;
  harsh.baseIllnessRate = 0.5f;
  harsh.healthCoverageReduction = 0.1f;

  const SimTickMetrics mildLast = runWithParams(mild);
  const SimTickMetrics harshLast = runWithParams(harsh);

  EXPECT_GT(harshLast.illnessRate, mildLast.illnessRate);
}

// Natural disasters (earthquake/flood) are gated by the same enableDisasters
// flag as fire, and off by default - a normally growing city should never
// report one.
TEST(CitySimulatorTests, DisastersDisabledByDefaultMeansNoEarthquakesOrFloodsEver) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 80, SimOptions{});

  ASSERT_FALSE(result.rows.empty());
  for (const SimTickMetrics& row : result.rows) {
    EXPECT_FALSE(row.earthquakeOccurred);
    EXPECT_FALSE(row.floodOccurred);
    EXPECT_EQ(row.buildingsLostToDisaster, 0u);
  }
}

// With disasters enabled and an elevated earthquake risk, a city growing
// over many ticks should end up with real cumulative disaster losses -
// proving the integration actually destroys buildings within the live
// simulation, not just in NaturalDisasterSystem's own unit tests.
TEST(CitySimulatorTests, ElevatedEarthquakeRiskDestroysBuildingsOverTime) {
  CityMap map({64, 64});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  SimOptions options = fastOptions();
  options.enableDisasters = true;
  options.disasterParams.earthquakeChancePerTick = 0.15f;  // far above the default, for a fast, reliable test
  options.disasterParams.earthquakeDestructionChance = 1.0f;

  const SimResult result = CitySimulator::run(map, roads, store, population, 7, 60, options);

  ASSERT_FALSE(result.rows.empty());
  EXPECT_GT(result.rows.back().buildingsLostToDisaster, 0u);

  // Cumulative losses must be monotonically non-decreasing tick to tick.
  uint32_t previous = 0;
  for (const SimTickMetrics& row : result.rows) {
    EXPECT_GE(row.buildingsLostToDisaster, previous);
    previous = row.buildingsLostToDisaster;
  }
}
