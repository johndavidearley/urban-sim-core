#include "gtest/gtest.h"
#include "src/systems/EnvironmentSystem.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/persistence/ReplayVerifier.hpp"
#include <cmath>
#include <limits>

namespace {
struct River {
  CityMap map{{9,11}};
  RoadNetwork roads{map};
  EntityStore store;
  PopulationStore population;
  River() {
    for(int y=1;y<10;++y) map.getTile({3,y}).type=2;
    EnvironmentSystem::initialize(map,42);
    for(int y=1;y<10;++y) map.environment({3,y}).bedMeters=-.5f*y;
  }
  void step(const std::vector<ServiceFacility>& facilities={}) {
    EnvironmentSystem::step(map,roads,store,facilities,42);
  }
  double total(bool mass=false) const {
    double value=0;
    for(int y=0;y<11;++y) for(int x=0;x<9;++x) {
      const auto& c=map.environment({x,y});value+=mass?c.contaminant:c.water;
    }
    return value;
  }
  uint64_t checksum() const {return ReplayVerifier::calculateChecksum(map,roads,store,population);}
};
}

TEST(Environment, ConservativeFlowTransportsPollutionDownstream) {
  River r;r.map.environment({3,3}).contaminant=100;
  for(int tick=0;tick<12;++tick) {
    const double water=r.total(),mass=r.total(true);r.step();const auto& state=r.map.environment();
    EXPECT_NEAR(r.total(),water+state.inflow-state.outflow-state.withdrawal+state.discharge,1e-8);
    EXPECT_NEAR(r.total(true),mass+state.contaminantIn-state.contaminantOut-state.contaminantDecay,1e-9);
    for(int y=1;y<10;++y) {
      const auto& c=r.map.environment({3,y});EXPECT_GE(c.water,0);EXPECT_GE(c.contaminant,0);
      EXPECT_TRUE(std::isfinite(c.currentZ));
    }
  }
  EXPECT_GT(r.map.environment({3,4}).contaminant,r.map.environment({3,2}).contaminant);
  EXPECT_LT(EnvironmentSystem::cleanFraction(r.map,{2,4}),EnvironmentSystem::cleanFraction(r.map,{2,2}));
}

TEST(Environment, DisconnectedWaterRemainsClean) {
  River r;r.map.getTile({7,4}).type=2;r.map.environment({7,4}).water=5000;
  r.map.environment({3,3}).contaminant=100;
  for(int i=0;i<20;++i) r.step();
  EXPECT_DOUBLE_EQ(r.map.environment({7,4}).contaminant,0);
  EXPECT_DOUBLE_EQ(r.map.environment({7,4}).water,5000);
}

TEST(Environment, IntakesShareAvailableWaterAndAccountForContamination) {
  CityMap map({7,7});map.getTile({3,3}).type=2;EnvironmentSystem::initialize(map,42);
  map.environment({3,3}).water=10;
  ServiceFacility a;a.type=ServiceType::Water;a.surfaceWater=true;a.position={2,3};
  ServiceFacility b=a;b.position={4,3};
  auto capacities=EnvironmentSystem::waterCapacities(map,{a,b});
  EXPECT_FLOAT_EQ(capacities[0],5);EXPECT_FLOAT_EQ(capacities[1],5);
  map.environment({3,3}).contaminant=.25;
  capacities=EnvironmentSystem::waterCapacities(map,{b,a});
  EXPECT_FLOAT_EQ(capacities[0],2.5);EXPECT_FLOAT_EQ(capacities[1],2.5);
  map.environment({3,3}).water=0;
  EXPECT_FLOAT_EQ(EnvironmentSystem::waterCapacities(map,{a,b})[0],0);
}

TEST(Environment, WindCurveAndWeatherAreDeterministic) {
  EXPECT_FLOAT_EQ(EnvironmentSystem::windPowerFactor(2),0);
  EXPECT_FLOAT_EQ(EnvironmentSystem::windPowerFactor(3),0);
  EXPECT_GT(EnvironmentSystem::windPowerFactor(8),0);
  EXPECT_LT(EnvironmentSystem::windPowerFactor(8),1);
  EXPECT_FLOAT_EQ(EnvironmentSystem::windPowerFactor(12),1);
  EXPECT_FLOAT_EQ(EnvironmentSystem::windPowerFactor(25),0);
  EXPECT_FLOAT_EQ(EnvironmentSystem::windPowerFactor(std::numeric_limits<float>::quiet_NaN()),0);
  River a,b;const float initial=a.map.environment().windX;
  for(int i=0;i<60;++i) {a.step();b.step();EXPECT_EQ(a.checksum(),b.checksum());}
  EXPECT_NE(initial,a.map.environment().windX);
  auto& cell=a.map.environment({2,2});cell.windX=12;cell.windZ=0;
  EXPECT_FLOAT_EQ(EnvironmentSystem::windPowerFactor(a.map,{2,2}),1);
  cell.windX=26;EXPECT_FLOAT_EQ(EnvironmentSystem::windPowerFactor(a.map,{2,2}),0);
}

TEST(Environment, BlockedDrainAccumulatesBacklogAndTreatmentReducesDischargeMass) {
  River r;const auto id=r.store.createBuilding(BuildingType::Residential,{1,3},10000);
  r.store.getBuilding(id)->occupancy=10000;r.map.getTile({1,3}).buildingId=static_cast<uint32_t>(id);
  r.map.getTile({1,3}).connectedToWater=true;
  r.roads.buildRoad({1,3},{2,3});
  ServiceFacility well;well.type=ServiceType::Water;well.position={1,3};
  ServiceFacility outlet;outlet.type=ServiceType::Sanitation;outlet.position={2,3};outlet.surfaceWater=true;
  r.map.environment({2,3}).terrainMeters=-100;
  r.step({well,outlet});EXPECT_NEAR(r.map.environment().sewageBacklog,16,1e-5);
  EXPECT_DOUBLE_EQ(r.map.environment().discharge,0);
  EXPECT_FLOAT_EQ(EnvironmentSystem::drainageFactor(r.map,{2,3}),0);
  r.map.environment({2,3}).terrainMeters=100;outlet.sewageTreatment=true;
  r.step({well,outlet});
  EXPECT_DOUBLE_EQ(r.map.environment().sewageBacklog,0);
  EXPECT_NEAR(r.map.environment().discharge,32,1e-5);
  EXPECT_NEAR(r.map.environment().contaminantIn,.64,1e-5);
  outlet.sewageTreatment=false;r.step({well,outlet});
  EXPECT_NEAR(r.map.environment().contaminantIn,r.map.environment().discharge,1e-9);
}

TEST(Environment, ReadOnlyQueriesDoNotAdvanceState) {
  River r;const auto before=r.checksum();
  for(int i=0;i<10;++i) {
    EnvironmentSystem::cleanFraction(r.map,{2,3});EnvironmentSystem::drainageFactor(r.map,{2,3});
    EnvironmentSystem::windPowerFactor(r.map,{2,3});EnvironmentSystem::waterCapacities(r.map,{});
    ServiceCoverageCache cache;ServiceSystem::buildCache(r.roads,{},cache);
  }
  EXPECT_EQ(r.checksum(),before);EXPECT_EQ(r.map.environment().tick,0u);
}

TEST(Environment, SnapshotRestoresExactContinuationAndRejectsInvalidWater) {
  River original;original.map.environment({3,3}).contaminant=100;
  for(int i=0;i<7;++i) original.step();
  auto snapshot=SaveLoadSystem::captureSnapshot(original.map,original.roads,original.store,original.population);
  River loaded;std::string error;
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot,loaded.map,loaded.roads,loaded.store,loaded.population,&error))<<error;
  ASSERT_EQ(original.checksum(),loaded.checksum());
  for(int i=0;i<15;++i) {original.step();loaded.step();EXPECT_EQ(original.checksum(),loaded.checksum());}
  const auto before=loaded.checksum();snapshot.tiles[0].environment.water=-1;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,loaded.map,loaded.roads,loaded.store,loaded.population,&error));
  EXPECT_EQ(before,loaded.checksum());
}

TEST(Environment, AirPollutionMovesDownwindAndCanBeExplicitlyDisabled) {
  River r;r.map.pollution({4,5})=1;
  EnvironmentSystem::step(r.map,r.roads,r.store,{},42,false);
  EXPECT_FLOAT_EQ(r.map.pollution({4,5}),1);
  r.step();const auto& c=r.map.environment({4,5});
  const Coord downwind{4+(c.windX>=0?1:-1),5};
  EXPECT_GT(r.map.pollution(downwind),0);EXPECT_LT(r.map.pollution({4,5}),1);
  double sum=0;for(int y=0;y<11;++y) for(int x=0;x<9;++x) sum+=r.map.pollution({x,y});
  EXPECT_NEAR(sum,1,1e-6);
}

TEST(Environment, ServiceCacheUsesLocalWindWithoutChangingNameplateCapacity) {
  River r;ServiceFacility wind;wind.type=ServiceType::Power;wind.powerSource=PowerSourceType::Wind;
  wind.position={2,2};wind.powerCapacityMW=10;
  auto& cell=r.map.environment(wind.position);cell.windX=12;cell.windZ=0;
  ServiceCoverageCache cache;ServiceSystem::buildCache(r.roads,{wind},cache);
  EXPECT_FLOAT_EQ(cache.powerGenerationCapacityMW,10);
  EXPECT_TRUE(ServiceSystem::isCacheValid(r.roads,{wind},cache));
  r.step();EXPECT_FALSE(ServiceSystem::isCacheValid(r.roads,{wind},cache));
  cell.windX=0;cell.windZ=0;ServiceSystem::buildCache(r.roads,{wind},cache);
  EXPECT_FLOAT_EQ(cache.powerGenerationCapacityMW,0);
  EXPECT_FLOAT_EQ(wind.powerCapacityMW,10);
}

TEST(Environment, UpwindTerrainSheltersLocalWind) {
  River r;const Coord p{4,5};const auto before=r.map.environment(p);
  const float speed=std::hypot(before.windX,before.windZ);
  ASSERT_GT(speed,.01f);
  const Coord upwind{p.x-int(std::lround(before.windX/speed)),p.y-int(std::lround(before.windZ/speed))};
  r.map.environment(upwind).terrainMeters=100;
  EnvironmentSystem::updateWind(r.map,r.store);
  EXPECT_LT(std::hypot(r.map.environment(p).windX,r.map.environment(p).windZ),speed);
}

TEST(Environment, BoundaryReservoirsAreLedgeredAndDroughtStopsReplenishment) {
  CityMap map({5,5});for(int y=0;y<5;++y) map.getTile({2,y}).type=2;
  EnvironmentSystem::initialize(map,42);RoadNetwork roads(map);EntityStore store;
  map.environment({2,0}).water=100;map.environment({2,4}).water=6000;
  const auto total=[&]() {double sum=0;for(int y=0;y<5;++y) sum+=map.environment({2,y}).water;return sum;};
  const double before=total();EnvironmentSystem::step(map,roads,store,{},42);
  EXPECT_DOUBLE_EQ(map.environment().inflow,4900);EXPECT_DOUBLE_EQ(map.environment().outflow,1000);
  EXPECT_NEAR(total(),before+4900-1000,1e-9);
  map.environment().inflowScale=0;EnvironmentSystem::step(map,roads,store,{},42);
  EXPECT_DOUBLE_EQ(map.environment().inflow,0);
}

#include "src/gameplay/PipeTool.hpp"
#include "src/systems/UtilityNetworkSystem.hpp"
namespace {
struct PipeCity {
  CityMap map{{12,12}};RoadNetwork roads{map};EntityStore store;PopulationStore people;
  std::vector<ServiceFacility> facilities;
  EntityId home;
  PipeCity() {
    EnvironmentSystem::initialize(map,42);
    for(int y=0;y<12;++y) for(int x=0;x<12;++x) map.environment({x,y}).terrainMeters=10-y*.25f;
    home=store.createBuilding(BuildingType::Residential,{3,4},10000);
    store.getBuilding(home)->occupancy=10000;map.getTile({3,4}).buildingId=static_cast<uint32_t>(home);
    ServiceFacility well;well.type=ServiceType::Water;well.position={1,1};
    ServiceFacility outlet;outlet.type=ServiceType::Sanitation;outlet.position={1,8};
    facilities={well,outlet};lay({2,1},{2,8});
  }
  void lay(Coord a,Coord b,int kind=3,bool remove=false) {
    int64_t funds=100000;
    auto plan=PipeTool::plan(map,glm::vec2(a)+glm::vec2{.5f,.5f},glm::vec2(b)+glm::vec2{.5f,.5f},kind,true,remove,funds);
    ASSERT_TRUE(plan.valid)<<plan.error;ASSERT_TRUE(PipeTool::apply(map,plan,funds));
  }
  void step() {EnvironmentSystem::step(map,roads,store,facilities,42);}
  uint64_t checksum() const {return ReplayVerifier::calculateChecksum(map,roads,store,people);}
};
}

TEST(PipeNetworks, RoadlessPipesDeliverWaterAndDrainSewage) {
  PipeCity city;city.step();
  EXPECT_TRUE(city.map.getTile({3,4}).connectedToWater);
  EXPECT_TRUE(city.map.getTile({3,4}).connectedToSewer);
  EXPECT_NEAR(city.map.utility({3,4}).waterDelivered,20,1e-5);
  EXPECT_NEAR(city.map.environment().sewageGenerated,16,1e-5);
  EXPECT_NEAR(city.map.environment().sewageHandled,16,1e-5);
  EXPECT_DOUBLE_EQ(city.map.environment().sewageBacklog,0);
  auto summary=ServiceSystem::evaluateCoverage(city.store,city.roads,city.facilities);
  EXPECT_FLOAT_EQ(summary.waterCoverage,1);EXPECT_FLOAT_EQ(summary.sanitationCoverage,1);
  EXPECT_FLOAT_EQ(summary.waterSupplyRatio,1);
}

TEST(PipeNetworks, SeveredSewerBacksUpAndRepairDrainsStoredSewage) {
  PipeCity city;city.lay({2,6},{2,6},2,true);city.step();
  EXPECT_TRUE(city.map.getTile({3,4}).connectedToWater);
  EXPECT_FALSE(city.map.getTile({3,4}).connectedToSewer);
  EXPECT_NEAR(city.map.utility({3,4}).sewageBacklog,16,1e-5);
  EXPECT_NEAR(city.map.environment().sewageBacklog,16,1e-5);
  city.lay({2,6},{2,6},2);city.step();
  EXPECT_TRUE(city.map.getTile({3,4}).connectedToSewer);
  EXPECT_DOUBLE_EQ(city.map.environment().sewageBacklog,0);
  EXPECT_NEAR(city.map.environment().sewageHandled,32,1e-5);
}

TEST(PipeNetworks, SeveredWaterStopsDeliveryAndSewageGeneration) {
  PipeCity city;city.lay({2,2},{2,2},1,true);city.step();
  EXPECT_FALSE(city.map.getTile({3,4}).connectedToWater);
  EXPECT_FLOAT_EQ(city.map.utility({3,4}).waterDelivered,0);
  EXPECT_DOUBLE_EQ(city.map.environment().sewageGenerated,0);
  auto summary=ServiceSystem::evaluateCoverage(city.store,city.roads,city.facilities);
  EXPECT_FLOAT_EQ(summary.waterCoverage,0);EXPECT_FLOAT_EQ(summary.waterSupplyRatio,0);
}

TEST(PipeNetworks, SharedTrunkCapacityIsNotDuplicatedPerBuilding) {
  PipeCity city;city.facilities[0].waterSupplyUnits=200;
  city.store.getBuilding(city.home)->capacity=30000;city.store.getBuilding(city.home)->occupancy=30000;
  const auto second=city.store.createBuilding(BuildingType::Residential,{3,5},30000);
  city.store.getBuilding(second)->occupancy=30000;city.map.getTile({3,5}).buildingId=static_cast<uint32_t>(second);
  const auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_NEAR(r.sourceUsed[0],100,1e-5);
  EXPECT_NEAR(r.delivered[4*12+3],60,1e-5);EXPECT_NEAR(r.delivered[5*12+3],40,1e-5);
  EXPECT_TRUE(r.waterConnected[4*12+3]);EXPECT_FALSE(r.waterConnected[5*12+3]);
}

TEST(PipeNetworks, PressureCannotCrossHighTerrainAndSewersCannotClimb) {
  PipeCity city;city.map.environment({2,2}).terrainMeters=100;
  auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_DOUBLE_EQ(r.sourceUsed[0],0);
  city.map.environment({2,2}).terrainMeters=9.5f;
  city.map.environment({2,6}).terrainMeters=100;
  r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_GT(r.sourceUsed[0],0);EXPECT_DOUBLE_EQ(r.outletUsed[1],0);
  EXPECT_GT(r.totalBacklog,0);
}

TEST(PipeNetworks, DisconnectedOutletsAndZeroFundingCannotDrainAnotherCatchment) {
  PipeCity city;city.facilities[1].position={10,10};city.step();
  EXPECT_NEAR(city.map.environment().sewageHandled,0,1e-7);
  city.facilities[1].position={1,8};city.facilities[1].quality=0;city.step();
  EXPECT_NEAR(city.map.environment().sewageBacklog,32,1e-5);
}

TEST(PipeNetworks, PreviewsAndRefreshNeverAdvanceBacklogOrWater) {
  PipeCity city;city.lay({2,6},{2,6},2,true);city.step();
  UtilityNetworkSystem::refresh(city.map,city.store,city.facilities);
  const auto before=city.checksum();
  for(int i=0;i<5;++i) {
    PipeTool::plan(city.map,{2.5f,6.5f},{2.5f,6.5f},2,true,false,1000);
    UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
    UtilityNetworkSystem::refresh(city.map,city.store,city.facilities);
  }
  EXPECT_EQ(city.checksum(),before);EXPECT_EQ(city.map.environment().tick,1u);
  EXPECT_NEAR(city.map.utility({3,4}).sewageBacklog,16,1e-5);
}

TEST(PipeNetworks, SaveLoadPreservesTopologyBacklogAndContinuation) {
  PipeCity original;original.lay({2,6},{2,6},2,true);original.step();
  const auto snapshot=SaveLoadSystem::captureSnapshot(original.map,original.roads,original.store,original.people);
  PipeCity loaded;std::string error;
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot,loaded.map,loaded.roads,loaded.store,loaded.people,&error))<<error;
  EXPECT_EQ(original.checksum(),loaded.checksum());
  for(int i=0;i<5;++i) {original.step();loaded.step();EXPECT_EQ(original.checksum(),loaded.checksum());}
  auto invalid=snapshot;invalid.tiles[0].utility.sewageBacklog=-1;const auto before=loaded.checksum();
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(invalid,loaded.map,loaded.roads,loaded.store,loaded.people,&error));
  EXPECT_EQ(before,loaded.checksum());
}

TEST(PipeTool, PlansAreAtomicRevalidatedAndChargedOnlyForChangedTypes) {
  CityMap map({12,12});int64_t funds=10000;
  auto plan=PipeTool::plan(map,{1.5f,1.5f},{5.5f,1.5f},3,true,false,funds);
  ASSERT_TRUE(plan.valid);EXPECT_EQ(plan.cost,350);ASSERT_TRUE(PipeTool::apply(map,plan,funds));
  EXPECT_EQ(funds,9650);EXPECT_FALSE(PipeTool::apply(map,plan,funds));EXPECT_EQ(funds,9650);
  const auto removal=PipeTool::plan(map,{3.5f,1.5f},{3.5f,1.5f},1,true,true,funds);
  ASSERT_TRUE(PipeTool::apply(map,removal,funds));EXPECT_FALSE(map.utility({3,1}).waterPipe);
  EXPECT_TRUE(map.utility({3,1}).sewerPipe);EXPECT_EQ(funds,9640);
  const auto repair=PipeTool::plan(map,{1.5f,1.5f},{5.5f,1.5f},3,true,false,funds);
  EXPECT_EQ(repair.cost,40);
  auto forged=repair;forged.cost=0;EXPECT_FALSE(PipeTool::apply(map,forged,funds));
  EXPECT_FALSE(PipeTool::plan(map,{-1,2},{5,5},3,true,false,funds).valid);
  EXPECT_FALSE(PipeTool::plan(map,{1,2},{5,5},3,true,false,0).valid);
}

TEST(PipeTool, FreeformRoutesRetainCardinalConnectionsAndDifferFromSnappedRoutes) {
  CityMap map({12,12});
  const auto free=PipeTool::plan(map,{1.2f,1.8f},{8.8f,7.2f},3,false,false,100000);
  const auto snapped=PipeTool::plan(map,{1.2f,1.8f},{8.8f,7.2f},3,true,false,100000);
  ASSERT_TRUE(free.valid);ASSERT_TRUE(snapped.valid);EXPECT_NE(free.tiles,snapped.tiles);
  for(size_t i=1;i<free.tiles.size();++i) EXPECT_EQ(std::abs(free.tiles[i].x-free.tiles[i-1].x)+std::abs(free.tiles[i].y-free.tiles[i-1].y),1);
}

TEST(PipeNetworks, LegacyUpgradeRetainsExistingSewage) {
  CityMap map({8,8});map.environment().sewageBacklog=15;int64_t funds=1000;
  ASSERT_FALSE(map.pipeNetworksEnabled());
  auto plan=PipeTool::plan(map,{2.5f,2.5f},{2.5f,3.5f},3,true,false,funds);
  ASSERT_TRUE(PipeTool::apply(map,plan,funds));
  EXPECT_TRUE(map.pipeNetworksEnabled());EXPECT_DOUBLE_EQ(map.utility({2,2}).sewageBacklog,15);
}

TEST(PipeNetworks, SurfaceIntakeWithdrawsOnlyNetworkDeliveredDemand) {
  PipeCity city;city.map.getTile({0,1}).type=2;
  auto& water=city.map.environment({0,1});water.water=5000;water.contaminant=0;water.bedMeters=0;
  city.facilities[0].surfaceWater=true;
  city.step();
  EXPECT_NEAR(city.map.environment().withdrawal,20,1e-5);
  EXPECT_NEAR(water.water,4980,1e-5);
  city.lay({2,2},{2,2},1,true);city.step();
  EXPECT_DOUBLE_EQ(city.map.environment().withdrawal,0);
  EXPECT_NEAR(water.water,4980,1e-5);
}

namespace {
void addLift(PipeCity& city,Coord p={2,5}) {
  int64_t funds=10000;
  const auto position=glm::vec2(p)+glm::vec2{.5f,.5f};
  auto plan=PipeTool::plan(city.map,position,position,4,true,false,funds);
  ASSERT_TRUE(plan.valid)<<plan.error;ASSERT_TRUE(PipeTool::apply(city.map,plan,funds));
}
void powerLift(PipeCity& city) {
  ServiceFacility power;power.type=ServiceType::Power;power.position={1,2};city.facilities.push_back(power);
}
}
TEST(SewerLifts, PoweredLiftCrossesAnUphillReachAndStillRequiresAnOutlet) {
  PipeCity city;city.map.environment({2,6}).terrainMeters=16;
  city.map.environment({2,7}).terrainMeters=18;
  auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_DOUBLE_EQ(r.discharged,0);
  addLift(city);powerLift(city);
  r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_NEAR(r.discharged,16,1e-5);EXPECT_NEAR(r.liftUsed[5*12+2],16,1e-5);
  city.facilities[1].position={10,10};
  EXPECT_DOUBLE_EQ(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).discharged,0);
}
TEST(SewerLifts, LiftHeadIsNotRenewedAtEachPipeTile) {
  PipeCity city;city.map.environment({2,6}).terrainMeters=20;
  city.map.environment({2,7}).terrainMeters=35;addLift(city);powerLift(city);
  const auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_DOUBLE_EQ(r.discharged,0);EXPECT_GT(r.totalBacklog,0);
}
TEST(SewerLifts, CapacityIsSharedAcrossParcelsAndOutlets) {
  PipeCity city;city.map.environment({2,6}).terrainMeters=16;addLift(city);powerLift(city);
  city.store.getBuilding(city.home)->capacity=30000;city.store.getBuilding(city.home)->occupancy=30000;
  const auto second=city.store.createBuilding(BuildingType::Residential,{3,3},10000);
  city.store.getBuilding(second)->occupancy=10000;city.map.getTile({3,3}).buildingId=static_cast<uint32_t>(second);
  city.facilities.push_back(city.facilities[1]);
  const auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_NEAR(r.generated,64,1e-5);EXPECT_NEAR(r.discharged,50,1e-5);
  EXPECT_NEAR(r.totalBacklog,14,1e-5);EXPECT_DOUBLE_EQ(r.liftUsed[5*12+2],50);
}
TEST(SewerLifts, PowerLossStopsPumpingAndRestorationClearsBacklog) {
  PipeCity city;city.map.environment({2,6}).terrainMeters=16;addLift(city);
  city.step();EXPECT_NEAR(city.map.environment().sewageBacklog,16,1e-5);
  powerLift(city);city.map.getTile({2,5}).connectedToPower=false;
  city.step();EXPECT_NEAR(city.map.environment().sewageBacklog,32,1e-5);
  city.map.getTile({2,5}).connectedToPower=true;
  city.step();EXPECT_DOUBLE_EQ(city.map.environment().sewageBacklog,0);
  EXPECT_NEAR(city.map.utility({2,5}).liftFlow,48,1e-5);
  city.facilities.back().powerCapacityMW=0;
  city.step();EXPECT_NEAR(city.map.environment().sewageBacklog,16,1e-5);
}
TEST(SewerLifts, ChainedStationsProvideSeparateHeadAndCapacity) {
  PipeCity city;city.map.environment({2,6}).terrainMeters=23;
  city.map.environment({2,7}).terrainMeters=38;addLift(city);addLift(city,{2,6});powerLift(city);
  auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_NEAR(r.discharged,16,1e-5);
  EXPECT_NEAR(r.liftUsed[5*12+2],16,1e-5);EXPECT_NEAR(r.liftUsed[6*12+2],16,1e-5);
  city.map.getTile({2,6}).connectedToPower=false;
  EXPECT_DOUBLE_EQ(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).discharged,0);
}
TEST(SewerLifts, ConstructionRemovalAndUpkeepAreExplicitAndAtomic) {
  PipeCity city;int64_t funds=2500;
  EXPECT_FALSE(PipeTool::plan(city.map,{7.5f,7.5f},{7.5f,7.5f},4,true,false,funds).valid);
  EXPECT_FALSE(PipeTool::plan(city.map,{2.5f,5.5f},{2.5f,6.5f},4,true,false,funds).valid);
  auto plan=PipeTool::plan(city.map,{2.5f,5.5f},{2.5f,5.5f},4,true,false,funds);
  EXPECT_EQ(plan.cost,2500);ASSERT_TRUE(PipeTool::apply(city.map,plan,funds));EXPECT_EQ(funds,0);
  EXPECT_EQ(UtilityNetworkSystem::operatingCost(city.map),4);
  funds=1000;
  auto pipeRemoval=PipeTool::plan(city.map,{2.5f,5.5f},{2.5f,5.5f},2,true,true,funds);
  EXPECT_FALSE(pipeRemoval.valid);EXPECT_FALSE(PipeTool::apply(city.map,pipeRemoval,funds));
  auto removal=PipeTool::plan(city.map,{2.5f,5.5f},{2.5f,5.5f},4,true,true,funds);
  ASSERT_TRUE(PipeTool::apply(city.map,removal,funds));EXPECT_EQ(funds,900);
  EXPECT_TRUE(city.map.utility({2,5}).sewerPipe);EXPECT_TRUE(city.map.utility({2,5}).waterPipe);
  EXPECT_EQ(UtilityNetworkSystem::operatingCost(city.map),0);
}
TEST(SewerLifts, SaveLoadContinuesExactlyAndReadOnlyQueriesDoNotPump) {
  PipeCity original;original.map.environment({2,6}).terrainMeters=16;addLift(original);powerLift(original);original.step();
  const auto before=original.checksum();
  for(int i=0;i<4;++i) UtilityNetworkSystem::evaluate(original.map,original.store,original.facilities);
  EXPECT_EQ(before,original.checksum());
  auto snapshot=SaveLoadSystem::captureSnapshot(original.map,original.roads,original.store,original.people);
  PipeCity loaded;loaded.facilities=original.facilities;std::string error;
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot,loaded.map,loaded.roads,loaded.store,loaded.people,&error))<<error;
  EXPECT_TRUE(loaded.map.utility({2,5}).sewageLift);EXPECT_EQ(original.checksum(),loaded.checksum());
  for(int i=0;i<5;++i) {original.step();loaded.step();EXPECT_EQ(original.checksum(),loaded.checksum());}
  snapshot.tiles[0].utility.sewageLift=true;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,loaded.map,loaded.roads,loaded.store,loaded.people,&error));
}

namespace {
void addBooster(PipeCity& city,Coord p={2,2}) {
  int64_t funds=10000;const auto position=glm::vec2(p)+glm::vec2{.5f,.5f};
  const auto plan=PipeTool::plan(city.map,position,position,5,true,false,funds);
  ASSERT_TRUE(plan.valid)<<plan.error;ASSERT_TRUE(PipeTool::apply(city.map,plan,funds));
}
void makeHighWaterDistrict(PipeCity& city) {
  city.map.environment({2,2}).terrainMeters=18;
  city.map.environment({2,3}).terrainMeters=30;
  city.map.environment({2,4}).terrainMeters=35;
  city.map.environment({3,4}).terrainMeters=35;
}
}
TEST(WaterBoosters, ConnectedBoosterSuppliesHighDistrictAndReportsPressure) {
  PipeCity city;makeHighWaterDistrict(city);powerLift(city);
  EXPECT_DOUBLE_EQ(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).sourceUsed[0],0);
  addBooster(city);
  const auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_NEAR(r.sourceUsed[0],20,1e-5);EXPECT_NEAR(r.boosterUsed[2*12+2],20,1e-5);
  EXPECT_TRUE(r.waterConnected[4*12+3]);EXPECT_GT(r.waterPressure[4*12+3],0);
  EXPECT_LT(r.waterPressure[4*12+3],4);
}
TEST(WaterBoosters, BoosterNeverCreatesWaterWithoutAConnectedSource) {
  PipeCity city;makeHighWaterDistrict(city);powerLift(city);addBooster(city);
  city.facilities[0].waterSupplyUnits=0;
  EXPECT_DOUBLE_EQ(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).sourceUsed[0],0);
  city.facilities[0].waterSupplyUnits=100;city.lay({2,1},{2,1},1,true);
  const auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_DOUBLE_EQ(r.sourceUsed[0],0);EXPECT_DOUBLE_EQ(r.boosterUsed[2*12+2],0);
}
TEST(WaterBoosters, BoosterMustItselfReceiveUpstreamPressure) {
  PipeCity city;makeHighWaterDistrict(city);powerLift(city);addBooster(city,{2,3});
  EXPECT_DOUBLE_EQ(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).sourceUsed[0],0);
}
TEST(WaterBoosters, CapacityIsSharedAcrossSourcesAndBuildings) {
  PipeCity city;makeHighWaterDistrict(city);powerLift(city);addBooster(city);
  city.facilities[0].waterSupplyUnits=200;city.facilities.push_back(city.facilities[0]);
  city.store.getBuilding(city.home)->capacity=30000;city.store.getBuilding(city.home)->occupancy=30000;
  const auto second=city.store.createBuilding(BuildingType::Residential,{3,5},20000);
  city.store.getBuilding(second)->occupancy=20000;city.map.getTile({3,5}).buildingId=static_cast<uint32_t>(second);
  city.map.environment({3,5}).terrainMeters=35;city.map.environment({2,5}).terrainMeters=35;
  const auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_NEAR(r.sourceUsed[0]+r.sourceUsed.back(),75,1e-5);
  EXPECT_DOUBLE_EQ(r.boosterUsed[2*12+2],75);EXPECT_NEAR(r.delivered[4*12+3],60,1e-5);
  EXPECT_NEAR(r.delivered[5*12+3],15,1e-5);EXPECT_FALSE(r.waterConnected[5*12+3]);
}
TEST(WaterBoosters, PowerLossStopsOnlyBoostingAndRecoveryRestoresSupply) {
  PipeCity city;makeHighWaterDistrict(city);addBooster(city);powerLift(city);
  auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);EXPECT_GT(r.sourceUsed[0],0);
  city.map.getTile({2,2}).connectedToPower=false;
  r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_FALSE(r.boosterPowered[2*12+2]);EXPECT_DOUBLE_EQ(r.sourceUsed[0],0);
  city.map.getTile({2,2}).connectedToPower=true;city.facilities.back().powerCapacityMW=0;
  EXPECT_DOUBLE_EQ(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).sourceUsed[0],0);
  city.facilities.back().powerCapacityMW=100;
  EXPECT_GT(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).sourceUsed[0],0);
  // Low terrain continues to use the original water source even with an unpowered booster.
  city.map.environment({2,3}).terrainMeters=10;city.map.environment({2,4}).terrainMeters=10;city.map.environment({3,4}).terrainMeters=10;
  city.facilities.back().powerCapacityMW=0;
  EXPECT_GT(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).sourceUsed[0],0);
}
TEST(WaterBoosters, ChainingAddsFiniteHeadWithoutRenewingItPerTile) {
  PipeCity city;makeHighWaterDistrict(city);addBooster(city);powerLift(city);
  city.map.environment({2,4}).terrainMeters=50;city.map.environment({3,4}).terrainMeters=50;
  EXPECT_DOUBLE_EQ(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).sourceUsed[0],0);
  city.map.environment({2,3}).terrainMeters=31; // Leave headroom for pipe friction at 50m.
  addBooster(city,{2,3});
  const auto r=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  EXPECT_NEAR(r.sourceUsed[0],20,1e-5);EXPECT_NEAR(r.boosterUsed[2*12+2],20,1e-5);
  EXPECT_NEAR(r.boosterUsed[3*12+2],20,1e-5);
}
TEST(WaterBoosters, PlacementRemovalAndUpkeepPreserveBothPipesAndSewerLift) {
  PipeCity city;addLift(city,{2,2});int64_t funds=3000;
  auto plan=PipeTool::plan(city.map,{2.5f,2.5f},{2.5f,2.5f},5,true,false,funds);
  ASSERT_TRUE(plan.valid);EXPECT_EQ(plan.cost,3000);ASSERT_TRUE(PipeTool::apply(city.map,plan,funds));EXPECT_EQ(funds,0);
  EXPECT_EQ(UtilityNetworkSystem::operatingCost(city.map),9);
  funds=1000;
  EXPECT_FALSE(PipeTool::plan(city.map,{2.5f,2.5f},{2.5f,2.5f},1,true,true,funds).valid);
  auto removal=PipeTool::plan(city.map,{2.5f,2.5f},{2.5f,2.5f},5,true,true,funds);
  ASSERT_TRUE(PipeTool::apply(city.map,removal,funds));EXPECT_EQ(funds,900);
  EXPECT_TRUE(city.map.utility({2,2}).waterPipe);EXPECT_TRUE(city.map.utility({2,2}).sewerPipe);
  EXPECT_TRUE(city.map.utility({2,2}).sewageLift);EXPECT_FALSE(city.map.utility({2,2}).waterBooster);
  EXPECT_EQ(UtilityNetworkSystem::operatingCost(city.map),4);
}
TEST(WaterBoosters, SaveLoadAndReadOnlyQueriesPreserveExactContinuation) {
  PipeCity original;makeHighWaterDistrict(original);addBooster(original);powerLift(original);original.step();
  const auto before=original.checksum();
  for(int i=0;i<5;++i) UtilityNetworkSystem::evaluate(original.map,original.store,original.facilities);
  EXPECT_EQ(before,original.checksum());
  auto snapshot=SaveLoadSystem::captureSnapshot(original.map,original.roads,original.store,original.people);
  PipeCity loaded;loaded.facilities=original.facilities;std::string error;
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot,loaded.map,loaded.roads,loaded.store,loaded.people,&error))<<error;
  EXPECT_TRUE(loaded.map.utility({2,2}).waterBooster);EXPECT_EQ(original.checksum(),loaded.checksum());
  for(int i=0;i<4;++i) {original.step();loaded.step();EXPECT_EQ(original.checksum(),loaded.checksum());}
  snapshot.tiles[0].utility.waterBooster=true;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,loaded.map,loaded.roads,loaded.store,loaded.people,&error));
}

#include "src/gameplay/CityGuide.hpp"
TEST(CityGuide, NeverCompletesFromAPlacedSourceWithoutPipesOrDrainage) {
  PipeCity city;city.map.setZone({3,4},1);city.map.setZone({3,5},2);city.roads.buildRoad({4,4},{4,5});powerLift(city);
  UtilityNetworkSystem::refresh(city.map,city.store,city.facilities);
  // Both parcels must be served, including the commercial zone with no building.
  city.map.getTile({3,4}).connectedToPower=true;city.map.getTile({3,5}).connectedToPower=true;
  EXPECT_EQ(city_guide::evaluate(city.map,city.roads,city.facilities,1,10000,true,true),city_guide::Waste);
  city.lay({2,6},{2,6},2,true);UtilityNetworkSystem::refresh(city.map,city.store,city.facilities);
  EXPECT_EQ(city_guide::evaluate(city.map,city.roads,city.facilities,1,10000,true,true),city_guide::Drainage);
  city.lay({2,2},{2,2},1,true);UtilityNetworkSystem::refresh(city.map,city.store,city.facilities);
  EXPECT_EQ(city_guide::evaluate(city.map,city.roads,city.facilities,1,10000,true,true),city_guide::WaterRepair);
}
TEST(CityGuide, BudgetAndSaveRemainExplicitStepsAfterTheCityIsServiced) {
  PipeCity city;powerLift(city);ServiceFacility garbage;garbage.type=ServiceType::Garbage;city.facilities.push_back(garbage);
  city.map.setZone({3,4},1);city.map.setZone({3,5},2);city.roads.buildRoad({4,4},{4,5});
  UtilityNetworkSystem::refresh(city.map,city.store,city.facilities);
  EXPECT_EQ(city_guide::evaluate(city.map,city.roads,city.facilities,0,10000,false,false),city_guide::Time);
  EXPECT_EQ(city_guide::evaluate(city.map,city.roads,city.facilities,1,0,true,true),city_guide::Time);
  EXPECT_EQ(city_guide::evaluate(city.map,city.roads,city.facilities,1,10000,false,false),city_guide::Budget);
  EXPECT_EQ(city_guide::evaluate(city.map,city.roads,city.facilities,1,10000,true,false),city_guide::Save);
  EXPECT_EQ(city_guide::evaluate(city.map,city.roads,city.facilities,1,10000,true,true),city_guide::Complete);
}
TEST(PipeNetworks, ParcelLateralDropsIntoBuriedSewerWithoutChangingTrunkGravity) {
  PipeCity city;
  city.map.environment({3,4}).terrainMeters=city.map.environment({2,4}).terrainMeters-.1f;
  EXPECT_GT(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).discharged,0);
  city.map.environment({2,6}).terrainMeters=city.map.environment({2,5}).terrainMeters+.1f;
  EXPECT_DOUBLE_EQ(UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities).discharged,0);
}

TEST(SewerLifts, SharedCatchmentReachMatchesActualPressureRoutingForEveryParcel) {
  PipeCity city;addLift(city);addLift(city,{2,6});powerLift(city);
  city.map.environment({2,6}).terrainMeters=23;city.map.environment({2,7}).terrainMeters=38;
  city.facilities[0].waterSupplyUnits=0; // Isolate one tiny parcel backlog per query.
  const auto reach=UtilityNetworkSystem::evaluate(city.map,city.store,city.facilities);
  for(int y=0;y<12;++y) for(int x=0;x<12;++x) {
    CityMap trial=city.map;trial.utility({x,y}).sewageBacklog=.01;
    const auto actual=UtilityNetworkSystem::evaluate(trial,city.store,city.facilities);
    EXPECT_EQ(reach.sewerConnected[size_t(y)*12+x],actual.backlog[size_t(y)*12+x]<1e-7)<<x<<","<<y;
  }
}
