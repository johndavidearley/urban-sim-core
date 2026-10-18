#include "gtest/gtest.h"

#include "src/core/ThreadPool.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/BudgetPolicy.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/CitySimSupport.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/Zoning.hpp"

namespace {

void layEastRoad(RoadNetwork& roads, int y, int x0, int x1) {
  for (int x = x0; x < x1; ++x) {
    roads.buildRoad({x, y}, {x + 1, y});
  }
}

ServicePlan powerPlan(
  const CityMap& map,
  const RoadNetwork& roads,
  const std::vector<ServiceFacility>& facilities,
  PowerSourceType source,
  Coord at,
  int64_t funds
) {
  ServicePlacementOptions options;
  options.powerSource = source;
  return ServiceTool::plan(
    map, roads, facilities, ServiceType::Power, at, funds, options);
}

}  // namespace

TEST(ServiceToolTests, PlansRoadAccessibleFacilityWithTypeSpecificCostAndRange) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({4, 4}, {5, 4});
  std::vector<ServiceFacility> facilities;

  const ServicePlan plan = ServiceTool::plan(
    map, roads, facilities, ServiceType::Fire, {4, 3}, 10000
  );

  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.cost, 5000);
  EXPECT_EQ(plan.facility.maxTravelDistance, 16);
  EXPECT_EQ(plan.facility.position, Coord(4, 3));
}

TEST(ServiceToolTests, RejectsBlockedDuplicateDisconnectedAndUnaffordableSites) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({4, 4}, {5, 4});
  std::vector<ServiceFacility> facilities;

  map.getTile({4, 3}).type = 2;
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Fire, {4, 3}, 10000).valid);
  map.getTile({4, 3}).type = 0;
  map.getTile({4, 3}).buildingId = 9;
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Fire, {4, 3}, 10000).valid);
  map.getTile({4, 3}).buildingId = 0;
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Fire, {10, 10}, 10000).valid);

  facilities.push_back({ServiceType::Police, {4, 3}, 14, 1.0f});
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Health, {4, 3}, 10000).valid);
  EXPECT_FALSE(ServiceTool::plan(map, roads, {}, ServiceType::Health, {4, 3}, 100).valid);
}

TEST(ServiceToolTests, BuildsFacilityAndDeductsFunds) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({4, 4}, {5, 4});
  std::vector<ServiceFacility> facilities;
  int64_t funds = 10000;
  const ServicePlan plan = ServiceTool::plan(
    map, roads, facilities, ServiceType::Education, {4, 3}, funds
  );

  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  ASSERT_EQ(facilities.size(), 1u);
  EXPECT_EQ(facilities.front().type, ServiceType::Education);
  EXPECT_EQ(funds, 4500);
}

TEST(ServiceToolTests, FixturePlacementSkipsRoadAccessAndHonorsRangeOverride) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  std::vector<ServiceFacility> facilities;
  int64_t funds = 20000;
  ServicePlacementOptions options;
  options.requireRoadAccess = false;
  options.coverageDistanceOverride = 12;

  const ServicePlan plan = ServiceTool::plan(
    map, roads, facilities, ServiceType::Power, {10, 10}, funds, options
  );
  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.facility.maxTravelDistance, 12);
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  ASSERT_EQ(facilities.size(), 1u);
  EXPECT_EQ(facilities.front().position, Coord(10, 10));
  EXPECT_EQ(facilities.front().maxTravelDistance, 12);
  EXPECT_EQ(funds, 20000 - ServiceTool::constructionCost(ServiceType::Power));
}

TEST(ServiceToolTests, CivicTypesHaveDistinctPositiveOperatingCosts) {
  EXPECT_GT(ServiceTool::operatingCostPerTick(ServiceType::Fire), 0);
  EXPECT_GT(ServiceTool::operatingCostPerTick(ServiceType::Police), 0);
  EXPECT_GT(ServiceTool::operatingCostPerTick(ServiceType::Health), 0);
  EXPECT_GT(ServiceTool::operatingCostPerTick(ServiceType::Education), 0);
  EXPECT_NE(ServiceTool::operatingCostPerTick(ServiceType::Fire),
            ServiceTool::operatingCostPerTick(ServiceType::Health));
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Power), 12000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Water), 8000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Sanitation), 7000);
  EXPECT_GT(ServiceTool::coverageDistance(ServiceType::Power), 0);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Garbage), 9000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Recycling), 11000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Cemetery), 10000);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Crematorium), 14000);
}

TEST(ServiceToolTests, MustClearParkBeforePlacingFacility) {
  CityMap map({8, 8}); RoadNetwork roads(map);
  roads.buildRoad({2, 2}, {3, 2});
  map.getTile({2, 3}).zone = static_cast<int>(ZoneType::Park);
  const auto plan = ServiceTool::plan(map, roads, {}, ServiceType::Fire, {2, 3}, 50000);
  EXPECT_FALSE(plan.valid);
}

TEST(ServiceToolTests, PaidFacilityReplacesEmptyZoningBeforeSubsequentGrowth) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  EntityStore store;
  roads.buildRoad({4, 4}, {5, 4});
  map.setZone({4, 3}, static_cast<int>(ZoneType::Residential));
  map.setZone({5, 3}, static_cast<int>(ZoneType::Residential));
  std::vector<ServiceFacility> facilities;
  int64_t funds = 10000;
  const auto plan = ServiceTool::plan(map, roads, facilities,
                                      ServiceType::Education, {4, 3}, funds);
  ASSERT_TRUE(plan.valid);
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  EXPECT_EQ(map.zone({4, 3}), static_cast<int>(ZoneType::None));
  EXPECT_EQ(map.zone({5, 3}), static_cast<int>(ZoneType::Residential));
  ZoneDemand demand;
  demand.residential = 1.f;
  for (uint32_t seed = 0; seed < 20; ++seed)
    GrowthSystem::runStep(map, store, demand, seed, 1.f, nullptr, {-1, -1},
                          {-1, -1}, nullptr, false, &roads);
  EXPECT_EQ(map.getTile({4, 3}).buildingId, 0u);
  EXPECT_NE(map.getTile({5, 3}).buildingId, 0u);
}
TEST(ServiceToolTests, RejectedPurchasePreservesZoningAndFunds) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({4, 4}, {5, 4});
  map.setZone({4, 3}, static_cast<int>(ZoneType::Residential));
  std::vector<ServiceFacility> facilities;
  int64_t funds = 10000;
  const auto plan = ServiceTool::plan(map, roads, facilities,
                                      ServiceType::Education, {4, 3}, funds);
  funds = 100;
  EXPECT_FALSE(ServiceTool::build(map, roads, facilities, plan, funds));
  EXPECT_EQ(map.zone({4, 3}), static_cast<int>(ZoneType::Residential));
  EXPECT_EQ(funds, 100);
  EXPECT_TRUE(facilities.empty());
}

TEST(ServiceToolTests,
     ShoreUtilitiesRequireLandWaterRoadAndRevalidateTheShore) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  for (int x = 4; x < 6; ++x)
    roads.buildRoad({x, 4}, {x + 1, 4});
  map.getTile({5, 2}).type = 2;
  ServicePlacementOptions options;
  options.surfaceWater = true;
  int64_t funds = 10000;
  std::vector<ServiceFacility> facilities;
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Water,
                                 {4, 3}, funds, options)
                   .valid);
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Water,
                                 {5, 2}, funds, options)
                   .valid);
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Fire,
                                 {5, 3}, funds, options)
                   .valid);
  const auto plan = ServiceTool::plan(
      map, roads, facilities, ServiceType::Water, {5, 3}, funds, options);
  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.cost, 3000);
  EXPECT_TRUE(plan.facility.surfaceWater);
  map.getTile({5, 2}).type = 0;
  EXPECT_FALSE(ServiceTool::build(map, roads, facilities, plan, funds));
  EXPECT_EQ(funds, 10000);
  EXPECT_TRUE(facilities.empty());
  map.getTile({5, 2}).type = 2;
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  EXPECT_EQ(funds, 7000);
  EXPECT_TRUE(facilities.front().surfaceWater);
  EXPECT_EQ(ServiceTool::operatingCostPerTick(ServiceType::Water, true), 12);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Sanitation, true), 2000);
  EXPECT_EQ(ServiceTool::operatingCostPerTick(ServiceType::Sanitation, true),
            8);
}

TEST(ServiceToolTests, IncineratorPurchaseRequiresRoadAndFunds) {
  CityMap map({16,16}); RoadNetwork roads(map);
  roads.buildRoad({4,4}, {5,4});
  std::vector<ServiceFacility> facilities;
  int64_t funds = 20000;
  const auto plan = ServiceTool::plan(map, roads, facilities, ServiceType::Incinerator, {4,3}, funds);
  ASSERT_TRUE(plan.valid);
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  EXPECT_EQ(funds, 2000);
  EXPECT_EQ(ServiceTool::operatingCostPerTick(ServiceType::Incinerator), 60);
  EXPECT_FALSE(ServiceTool::plan(map, roads, facilities, ServiceType::Incinerator, {5,3}, funds).valid);
}

TEST(ServiceToolTests, PowerPlantCatalogMatchesTheMilestone) {
  struct Row {
    PowerSourceType source;
    int64_t cost;
    int64_t upkeep;
    float mw;
    float kg;
    bool shore;
    int span;
  };
  const Row rows[] = {
    {PowerSourceType::Generic, 12000, 45, 100.f, 400.f, false, 1},
    {PowerSourceType::Coal, 10000, 40, 80.f, 1000.f, false, 1},
    {PowerSourceType::NaturalGas, 14000, 36, 60.f, 450.f, false, 1},
    {PowerSourceType::Nuclear, 80000, 90, 200.f, 12.f, false, 1},
    {PowerSourceType::Solar, 8000, 8, 15.f, 45.f, false, 2},
    {PowerSourceType::Wind, 6000, 6, 10.f, 11.f, false, 1},
    {PowerSourceType::Hydro, 18000, 18, 40.f, 24.f, true, 1},
    {PowerSourceType::Substation, 1500, 4, 0.f, 0.f, false, 1},
  };

  CityMap map({20, 12});
  RoadNetwork roads(map);
  layEastRoad(roads, 4, 1, 18);
  std::vector<ServiceFacility> facilities;
  int64_t funds = 200000;
  const Coord sites[] = {{2, 3}, {4, 3}, {6, 3}, {8, 3}, {12, 5}, {10, 3}};
  const PowerSourceType placed[] = {
    PowerSourceType::Generic, PowerSourceType::Coal, PowerSourceType::NaturalGas,
    PowerSourceType::Nuclear, PowerSourceType::Solar, PowerSourceType::Wind
  };

  for (const Row& row : rows) {
    const PowerPlantSpec spec = ServiceTool::powerPlant(row.source);
    EXPECT_EQ(spec.constructionCost, row.cost);
    EXPECT_EQ(spec.operatingCost, row.upkeep);
    EXPECT_FLOAT_EQ(spec.capacityMW, row.mw);
    EXPECT_FLOAT_EQ(spec.emissionsKgPerMWh, row.kg);
    EXPECT_EQ(spec.shoreline, row.shore);
    EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Power, false, row.source), row.cost);
    EXPECT_EQ(ServiceTool::operatingCostPerTick(ServiceType::Power, false, row.source), row.upkeep);
    EXPECT_FLOAT_EQ(ServiceSystem::defaultPowerEmissions(row.source), row.kg);
  }
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Power), 12000);
  EXPECT_EQ(ServiceTool::operatingCostPerTick(ServiceType::Power), 45);

  for (size_t i = 0; i < 6; ++i) {
    const ServicePlan plan = powerPlan(map, roads, facilities, placed[i], sites[i], funds);
    ASSERT_TRUE(plan.valid) << plan.error;
    const Row* row = nullptr;
    for (const Row& candidate : rows) {
      if (candidate.source == placed[i]) row = &candidate;
    }
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(plan.cost, row->cost);
    EXPECT_FLOAT_EQ(plan.facility.powerCapacityMW, row->mw);
    EXPECT_FLOAT_EQ(plan.facility.emissionsKgPerMWh, row->kg);
    EXPECT_EQ(plan.facility.powerSource, placed[i]);
    EXPECT_FALSE(plan.facility.surfaceWater);
    EXPECT_EQ(facilityFootprintSpan(plan.facility), row->span);
    ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  }
  EXPECT_EQ(facilities.size(), 6u);
  EXPECT_EQ(funds, 200000 - (12000 + 10000 + 14000 + 80000 + 8000 + 6000));
}

TEST(ServiceToolTests, HydroRequiresShoreAndDoesNotBecomeAnIntake) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  layEastRoad(roads, 4, 3, 8);
  int64_t funds = 100000;

  const ServicePlan inland = powerPlan(map, roads, {}, PowerSourceType::Hydro, {4, 3}, funds);
  EXPECT_FALSE(inland.valid);
  EXPECT_EQ(inland.error, "hydro plant requires land beside a lake or river");

  map.getTile({5, 2}).type = 2;
  const ServicePlan diagonal = powerPlan(map, roads, {}, PowerSourceType::Hydro, {4, 3}, funds);
  EXPECT_FALSE(diagonal.valid);
  EXPECT_EQ(diagonal.error, "hydro plant requires land beside a lake or river");

  map.getTile({5, 2}).type = 0;
  map.getTile({4, 2}).type = 2;
  const ServicePlan onWater = powerPlan(map, roads, {}, PowerSourceType::Hydro, {4, 2}, funds);
  EXPECT_FALSE(onWater.valid);

  const ServicePlan shore = powerPlan(map, roads, {}, PowerSourceType::Hydro, {4, 3}, funds);
  ASSERT_TRUE(shore.valid) << shore.error;
  EXPECT_EQ(shore.cost, 18000);
  EXPECT_FLOAT_EQ(shore.facility.powerCapacityMW, 40.f);
  EXPECT_FLOAT_EQ(shore.facility.emissionsKgPerMWh, 24.f);
  EXPECT_FALSE(shore.facility.surfaceWater);
  std::vector<ServiceFacility> facilities;
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, shore, funds));
  EXPECT_EQ(funds, 82000);
  ASSERT_EQ(facilities.size(), 1u);
  EXPECT_FALSE(facilities.front().surfaceWater);
  EXPECT_EQ(facilities.front().powerSource, PowerSourceType::Hydro);
  EXPECT_EQ(ServiceTool::operatingCostPerTick(
    ServiceType::Power, false, PowerSourceType::Hydro), 18);
}

TEST(ServiceToolTests, SolarFootprintRejectsPartialSitesAndClearsEveryTile) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  layEastRoad(roads, 2, 1, 8);
  int64_t funds = 50000;

  const ServicePlan offMap = powerPlan(map, roads, {}, PowerSourceType::Solar, {15, 3}, funds);
  EXPECT_FALSE(offMap.valid);
  EXPECT_EQ(offMap.error, "solar plant needs a clear 2x2");

  map.getTile({3, 4}).buildingId = 9;
  const ServicePlan occupied = powerPlan(map, roads, {}, PowerSourceType::Solar, {2, 3}, funds);
  EXPECT_FALSE(occupied.valid);
  EXPECT_EQ(occupied.error, "site is occupied");
  map.getTile({3, 4}).buildingId = 0;

  const Coord cells[] = {{2, 3}, {3, 3}, {2, 4}, {3, 4}};
  for (const Coord cell : cells) {
    map.setZone(cell, static_cast<int>(ZoneType::Residential));
  }
  std::vector<ServiceFacility> facilities;
  const ServicePlan plan = powerPlan(map, roads, facilities, PowerSourceType::Solar, {2, 3}, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.cost, 8000);
  EXPECT_EQ(facilityFootprintSpan(plan.facility), 2);
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  EXPECT_EQ(funds, 42000);
  for (const Coord cell : cells) {
    EXPECT_EQ(map.zone(cell), static_cast<int>(ZoneType::None));
    EXPECT_TRUE(facilityCoversTile(facilities.front(), cell));
  }
  EXPECT_FALSE(facilityCoversTile(facilities.front(), {4, 3}));

  const ServicePlan overlap = ServiceTool::plan(
    map, roads, facilities, ServiceType::Fire, {3, 4}, funds);
  EXPECT_FALSE(overlap.valid);
  EXPECT_EQ(overlap.error, "site already has a service");

  const ZonePlan zone = ZoneTool::plan(
    map, {3, 4}, {3, 4}, ZoneType::Residential, funds, &facilities);
  EXPECT_FALSE(zone.valid);
  EXPECT_EQ(zone.error, "area includes a service facility");

  const RoadPlan blocked = RoadTool::plan(
    map, roads, {2, 4}, {3, 4}, funds, RoadClass::Local, &facilities);
  EXPECT_FALSE(blocked.valid);
  EXPECT_EQ(blocked.error, "route crosses a solar plant");
  const RoadPlan legacyCall = RoadTool::plan(map, roads, {2, 4}, {3, 4}, funds);
  EXPECT_TRUE(legacyCall.valid) << legacyCall.error;

  EntityStore store;
  int64_t demoFunds = 1000;
  const BulldozePlan demo = BulldozeTool::plan(
    map, roads, facilities, {3, 4}, {3, 4}, demoFunds);
  ASSERT_TRUE(demo.valid) << demo.error;
  EXPECT_EQ(demo.services, 1);
  EXPECT_EQ(demo.buildings, 0);
  EXPECT_EQ(demo.cost, BulldozeTool::kCostPerService);
  ASSERT_TRUE(BulldozeTool::apply(map, roads, store, facilities, demo, demoFunds));
  EXPECT_TRUE(facilities.empty());
  EXPECT_EQ(demoFunds, 900);
}

TEST(ServiceToolTests, CoalAndHydroDifferInSupplyUpkeepAndLocalPollution) {
  auto paint = [](CityMap& map, RoadNetwork& roads, PowerSourceType source, bool water) {
    layEastRoad(roads, 4, 4, 11);
    if (water) map.getTile({4, 2}).type = 2;
    std::vector<ServiceFacility> facilities;
    int64_t funds = 50000;
    const ServicePlan plan = powerPlan(map, roads, facilities, source, {4, 3}, funds);
    EXPECT_TRUE(plan.valid) << plan.error;
    EXPECT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
    city_sim::applyPowerPlantPollution(map, roads, facilities, 0, 0, 15, 15);
    return facilities;
  };

  CityMap coalMap({16, 16});
  RoadNetwork coalRoads(coalMap);
  const std::vector<ServiceFacility> coal = paint(coalMap, coalRoads, PowerSourceType::Coal, false);
  CityMap hydroMap({16, 16});
  RoadNetwork hydroRoads(hydroMap);
  const std::vector<ServiceFacility> hydro = paint(hydroMap, hydroRoads, PowerSourceType::Hydro, true);

  ASSERT_EQ(coal.size(), 1u);
  ASSERT_EQ(hydro.size(), 1u);
  EXPECT_FLOAT_EQ(coal.front().powerCapacityMW, 80.f);
  EXPECT_FLOAT_EQ(hydro.front().powerCapacityMW, 40.f);
  EXPECT_FALSE(coal.front().surfaceWater);
  EXPECT_FALSE(hydro.front().surfaceWater);

  EXPECT_FLOAT_EQ(coalMap.pollution({4, 3}), 1.f);
  EXPECT_FLOAT_EQ(hydroMap.pollution({4, 3}), 0.024f);
  EXPECT_GT(coalMap.pollution({4, 3}), hydroMap.pollution({4, 3}));
  EXPECT_NEAR(coalMap.pollution({7, 3}), 4.f / 7.f, 1e-5f);
  EXPECT_NEAR(hydroMap.pollution({7, 3}), 0.024f * 4.f / 7.f, 1e-5f);
  EXPECT_GT(coalMap.pollution({7, 3}), hydroMap.pollution({7, 3}));
  EXPECT_GT(coalMap.pollution({4, 3}), coalMap.pollution({7, 3}));
  EXPECT_FLOAT_EQ(coalMap.pollution({12, 4}), 0.f);
  EXPECT_FLOAT_EQ(hydroMap.pollution({12, 4}), 0.f);

  const RoadPlan acrossCoal = RoadTool::plan(
    coalMap, coalRoads, {4, 3}, {5, 3}, 1000, RoadClass::Local, &coal);
  EXPECT_TRUE(acrossCoal.valid) << acrossCoal.error;

  BudgetPolicy policy;
  const size_t power = static_cast<size_t>(ServiceType::Power);
  EXPECT_EQ(policy.serviceCosts(coal)[power], 40);
  EXPECT_EQ(policy.serviceCosts(hydro)[power], 18);

  std::vector<ServiceFacility> both = coal;
  both.push_back(hydro.front());
  ServiceCoverageCache cache;
  ServiceSystem::buildCache(coalRoads, both, cache);
  EXPECT_FLOAT_EQ(cache.powerGenerationCapacityMW, 120.f);
  EXPECT_FLOAT_EQ(cache.powerWeightedEmissions, 80.f * 1000.f + 40.f * 24.f);
}

TEST(ServiceToolTests, GenericPlantAddsNoLocalPollutionAndKeepsTheCitywideReport) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  layEastRoad(roads, 4, 4, 11);
  std::vector<ServiceFacility> facilities;
  int64_t funds = 20000;
  const ServicePlan plan = powerPlan(map, roads, facilities, PowerSourceType::Generic, {4, 3}, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  EXPECT_EQ(facilities.front().powerSource, PowerSourceType::Generic);
  EXPECT_FLOAT_EQ(facilities.front().powerCapacityMW, 100.f);
  EXPECT_FLOAT_EQ(facilities.front().emissionsKgPerMWh, 400.f);

  city_sim::applyPowerPlantPollution(map, roads, facilities, 0, 0, 15, 15);
  const glm::ivec2 dims = map.getDimensions();
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      EXPECT_FLOAT_EQ(map.pollution({x, y}), 0.f);
    }
  }

  EntityStore store;
  const ServiceCoverageSummary summary = ServiceSystem::evaluateCoverage(store, roads, facilities);
  EXPECT_FLOAT_EQ(summary.powerGenerationMW, 100.f);
  EXPECT_FLOAT_EQ(summary.powerEmissionsKgPerMWh, 400.f);
}

TEST(ServiceToolTests, PowerFundingScalesCapacityAndUpkeepTogether) {
  CityMap map({12, 12});
  RoadNetwork roads(map);
  layEastRoad(roads, 4, 2, 6);
  std::vector<ServiceFacility> facilities;
  int64_t funds = 20000;
  const ServicePlan plan = powerPlan(map, roads, facilities, PowerSourceType::Coal, {3, 3}, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));

  const size_t power = static_cast<size_t>(ServiceType::Power);
  BudgetPolicy full;
  EXPECT_EQ(full.serviceCosts(facilities)[power], 40);
  const auto funded = full.fundedFacilities(facilities);
  ASSERT_EQ(funded.size(), 1u);
  EXPECT_FLOAT_EQ(funded.front().powerCapacityMW, 80.f);
  EXPECT_FLOAT_EQ(funded.front().emissionsKgPerMWh, 1000.f);

  BudgetPolicy half;
  half.servicePercent[power] = 50;
  EXPECT_EQ(half.serviceCosts(facilities)[power], 20);
  const auto halved = half.fundedFacilities(facilities);
  ASSERT_EQ(halved.size(), 1u);
  EXPECT_FLOAT_EQ(halved.front().powerCapacityMW, 40.f);
  EXPECT_FLOAT_EQ(halved.front().emissionsKgPerMWh, 1000.f);
  EXPECT_FLOAT_EQ(halved.front().quality, 0.5f);

  city_sim::applyPowerPlantPollution(map, roads, halved, 0, 0, 11, 11);
  const float fundedPollution = map.pollution({3, 3});
  EXPECT_GT(fundedPollution, 0.f);

  BudgetPolicy closed;
  closed.servicePercent[power] = 0;
  EXPECT_TRUE(closed.fundedFacilities(facilities).empty());
  EXPECT_EQ(closed.serviceCosts(facilities)[power], 0);
  for (int y = 0; y < 12; ++y) {
    for (int x = 0; x < 12; ++x) map.pollution({x, y}) = 0.f;
  }
  city_sim::applyPowerPlantPollution(
    map, roads, closed.fundedFacilities(facilities), 0, 0, 11, 11);
  EXPECT_FLOAT_EQ(map.pollution({3, 3}), 0.f);
  EXPECT_LT(0.f, fundedPollution);
}

TEST(ServiceToolTests, AutonomousPowerStaysGenericAndAvoidsSolarPads) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  for (int x = 0; x < 15; ++x) roads.buildRoad({x, 5}, {x + 1, 5});
  std::vector<ServiceFacility> facilities;
  int64_t funds = 20000;
  const ServicePlan plan = powerPlan(map, roads, facilities, PowerSourceType::Solar, {2, 6}, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  const ServiceFacility solar = facilities.front();

  city_sim::placeFacilitiesIfNeeded(map, facilities, 8000, {8, 8}, 7, 12, true, false);

  int genericPlants = 0;
  for (const ServiceFacility& facility : facilities) {
    if (facility.powerSource == PowerSourceType::Solar) continue;
    EXPECT_FALSE(facilityCoversTile(solar, facility.position));
    if (facility.type != ServiceType::Power) continue;
    ++genericPlants;
    EXPECT_EQ(facility.powerSource, PowerSourceType::Generic);
    EXPECT_FLOAT_EQ(facility.powerCapacityMW, 100.f);
    EXPECT_FLOAT_EQ(facility.emissionsKgPerMWh, 400.f);
    EXPECT_EQ(facilityFootprintSpan(facility), 1);
  }
  EXPECT_GE(genericPlants, 1);
}

TEST(ServiceToolTests, SolarPadsStayOutOfAutonomousZoning) {
  CityMap map({32, 32});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  city_sim::ConstructionState state;
  ThreadPool pool(1);
  ZoneDemand demand;
  demand.residential = 1.f;
  city_sim::ConstructionOptions options;
  options.gridSpacing = 4;
  options.zoneBatchPerTick = 0;
  options.placeFacilities = false;
  std::vector<ServiceFacility> facilities;
  std::vector<TransitRoute> routes;
  city_sim::expandConstruction(
    map, roads, store, population, facilities, routes, state, pool, demand, options);

  const Coord anchor{13, 13};
  for (int dy = 0; dy < 2; ++dy) {
    for (int dx = 0; dx < 2; ++dx) {
      map.setZone({anchor.x + dx, anchor.y + dy}, static_cast<int>(ZoneType::Residential));
    }
  }
  int64_t funds = 20000;
  const ServicePlan plan = powerPlan(map, roads, facilities, PowerSourceType::Solar, anchor, funds);
  ASSERT_TRUE(plan.valid) << plan.error;
  ASSERT_TRUE(ServiceTool::build(map, roads, facilities, plan, funds));
  for (int dy = 0; dy < 2; ++dy) {
    for (int dx = 0; dx < 2; ++dx) {
      EXPECT_EQ(map.zone({anchor.x + dx, anchor.y + dy}), static_cast<int>(ZoneType::None));
    }
  }

  options.zoneBatchPerTick = 100000;
  city_sim::expandConstruction(
    map, roads, store, population, facilities, routes, state, pool, demand, options);

  EXPECT_EQ(map.zone({14, 13}), static_cast<int>(ZoneType::None));
  EXPECT_EQ(map.zone({13, 14}), static_cast<int>(ZoneType::None));
  EXPECT_EQ(map.zone({14, 14}), static_cast<int>(ZoneType::None));
  EXPECT_EQ(map.zone(anchor), static_cast<int>(ZoneType::Residential));
}

TEST(ServiceToolTests, ShoreTreatmentReplacesRawOutletWithFullPriceAndUpkeep) {
  CityMap map({16,16}); RoadNetwork roads(map);
  map.getTile({8,6}).type = 2;
  roads.buildRoad({6,6},{6,7});
  ServiceFacility raw{ServiceType::Sanitation,{7,6},18,1.f}; raw.surfaceWater = true;
  std::vector<ServiceFacility> facilities{raw};
  ServicePlacementOptions options; options.surfaceWater = true; options.sewageTreatment = true;
  int64_t funds = 20000;
  const auto plan = ServiceTool::plan(map,roads,facilities,ServiceType::Sanitation,{7,6},funds,options);
  ASSERT_TRUE(plan.valid) << plan.error;
  EXPECT_EQ(plan.cost,9000);
  ASSERT_TRUE(ServiceTool::build(map,roads,facilities,plan,funds));
  ASSERT_EQ(facilities.size(),1u);
  EXPECT_TRUE(facilities[0].sewageTreatment);
  EXPECT_EQ(funds,11000);
  BudgetPolicy policy;
  EXPECT_EQ(policy.serviceCost(facilities),34);
  policy.servicePercent[6] = 50;
  EXPECT_EQ(policy.serviceCost(facilities),17);
  policy.servicePercent[6] = 0;
  EXPECT_EQ(policy.serviceCost(facilities),0);
  EXPECT_TRUE(policy.fundedFacilities(facilities).empty());
  EXPECT_FALSE(ServiceTool::build(map,roads,facilities,plan,funds));
  EXPECT_EQ(funds,11000);
}

TEST(ServiceToolTests, TreatmentRequiresSanitationShoreRoadAndFunds) {
  CityMap map({16,16}); RoadNetwork roads(map);
  ServicePlacementOptions options; options.surfaceWater = true; options.sewageTreatment = true;
  EXPECT_FALSE(ServiceTool::plan(map,roads,{},ServiceType::Water,{7,6},20000,options).valid);
  EXPECT_FALSE(ServiceTool::plan(map,roads,{},ServiceType::Sanitation,{7,6},20000,options).valid);
  map.getTile({8,6}).type = 2;
  EXPECT_FALSE(ServiceTool::plan(map,roads,{},ServiceType::Sanitation,{7,6},20000,options).valid);
  roads.buildRoad({6,6},{6,7});
  EXPECT_FALSE(ServiceTool::plan(map,roads,{},ServiceType::Sanitation,{7,6},8999,options).valid);
  EXPECT_TRUE(ServiceTool::plan(map,roads,{},ServiceType::Sanitation,{7,6},9000,options).valid);
  options.surfaceWater = false;
  EXPECT_FALSE(ServiceTool::plan(map,roads,{},ServiceType::Sanitation,{7,6},20000,options).valid);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Sanitation),7000);
  EXPECT_EQ(ServiceTool::operatingCostPerTick(ServiceType::Sanitation),26);
  EXPECT_EQ(ServiceTool::constructionCost(ServiceType::Sanitation,true),2000);
  EXPECT_EQ(ServiceTool::operatingCostPerTick(ServiceType::Sanitation,true),8);
}
