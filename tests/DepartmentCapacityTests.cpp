#include "gtest/gtest.h"
#include "src/gameplay/BudgetPolicy.hpp"
#include "src/gameplay/BuildingDiagnostics.hpp"
#include "src/systems/NeighborhoodSystem.hpp"
#include <algorithm>

namespace {
struct DepartmentCity {
  CityMap map{{32,20}};
  RoadNetwork roads{map};
  EntityStore store;
  std::vector<ServiceFacility> facilities;
  DepartmentCity() {
    for (int x=1;x<26;++x) roads.buildRoad({x,8},{x+1,8});
    for (auto type : {ServiceType::Fire,ServiceType::Police,ServiceType::Sanitation})
      facilities.push_back({type,{3,7},30,1.f});
  }
  EntityId add(Coord position,int occupancy,BuildingType type=BuildingType::Residential) {
    const auto id=store.createBuilding(type,position,occupancy);
    store.getBuilding(id)->occupancy=occupancy;
    map.getTile(position).buildingId=id;
    map.setZone(position,static_cast<int>(ZoneType::Residential));
    return id;
  }
  ServiceCoverageCache cache(bool bounded=true) {
    ServiceCoverageCache result;
    ServiceSystem::buildCache(roads,facilities,result,bounded);
    ServiceSystem::allocateDepartmentCapacity(store,roads,result);
    return result;
  }
};
}

TEST(DepartmentCapacityTests, NearestResidentsAndWorkersConsumeEachDepartmentIndependently) {
  DepartmentCity city;
  city.add({3,9},1500);
  city.add({12,9},1000);
  city.add({18,9},1000,BuildingType::Commercial);
  city.add({30,17},200);
  const auto cache=city.cache();
  EXPECT_DOUBLE_EQ(cache.departments[0].capacity,2000);
  EXPECT_DOUBLE_EQ(cache.departments[0].demand,3700);
  EXPECT_DOUBLE_EQ(cache.departments[0].reachable,3500);
  EXPECT_DOUBLE_EQ(cache.departments[0].served,2000);
  EXPECT_DOUBLE_EQ(cache.departments[0].unserved,1700);
  EXPECT_DOUBLE_EQ(cache.departments[1].served,2500);
  EXPECT_DOUBLE_EQ(cache.departments[2].served,3000);
  EXPECT_FLOAT_EQ(ServiceSystem::departmentFactorAt(cache,ServiceType::Fire,{3,9}),1.f);
  EXPECT_FLOAT_EQ(ServiceSystem::departmentFactorAt(cache,ServiceType::Fire,{12,9}),0.5f);
  EXPECT_FLOAT_EQ(ServiceSystem::departmentFactorAt(cache,ServiceType::Fire,{18,9}),0.f);
  EXPECT_FLOAT_EQ(ServiceSystem::departmentFactorAt(cache,ServiceType::Sanitation,{18,9}),0.5f);
  EXPECT_FLOAT_EQ(ServiceSystem::departmentFactorAt(cache,ServiceType::Fire,{30,17}),0.f);
}

TEST(DepartmentCapacityTests, AddedSitesRestoreReachableOccupantsAndFundingScalesCapacity) {
  DepartmentCity city;
  city.add({3,9},1500);city.add({12,9},1000);city.add({18,9},1000,BuildingType::Industrial);
  city.facilities.push_back({ServiceType::Fire,{18,7},30,1.f});
  auto cache=city.cache();
  EXPECT_DOUBLE_EQ(cache.departments[0].capacity,4000);
  EXPECT_DOUBLE_EQ(cache.departments[0].served,3500);
  EXPECT_DOUBLE_EQ(cache.departments[0].unserved,0);
  BudgetPolicy policy;policy.servicePercent[static_cast<size_t>(ServiceType::Fire)]=50;
  auto funded=policy.fundedFacilities(city.facilities);
  ServiceSystem::buildCache(city.roads,funded,cache,true);
  ServiceSystem::allocateDepartmentCapacity(city.store,city.roads,cache);
  EXPECT_DOUBLE_EQ(cache.departments[0].capacity,2000);
  EXPECT_DOUBLE_EQ(cache.departments[0].served,2000);
  policy.servicePercent[static_cast<size_t>(ServiceType::Fire)]=0;
  ServiceSystem::buildCache(city.roads,policy.fundedFacilities(city.facilities),cache,true);
  ServiceSystem::allocateDepartmentCapacity(city.store,city.roads,cache);
  EXPECT_DOUBLE_EQ(cache.departments[0].capacity,0);
  EXPECT_DOUBLE_EQ(cache.departments[0].served,0);
  EXPECT_DOUBLE_EQ(cache.departments[0].unserved,3500);
}

TEST(DepartmentCapacityTests, ReallocationSeesInPlaceOccupancyEditsAndDisconnectedSitesSupplyNothing) {
  DepartmentCity city;
  const auto id=city.add({3,9},3000);
  auto cache=city.cache();
  EXPECT_FLOAT_EQ(ServiceSystem::departmentFactorAt(cache,ServiceType::Fire,{3,9}),2.f/3.f);
  city.store.getBuilding(id)->occupancy=400;
  ServiceSystem::allocateDepartmentCapacity(city.store,city.roads,cache);
  EXPECT_DOUBLE_EQ(cache.departments[0].demand,400);
  EXPECT_DOUBLE_EQ(cache.departments[0].served,400);
  EXPECT_FLOAT_EQ(ServiceSystem::departmentFactorAt(cache,ServiceType::Fire,{3,9}),1.f);
  city.facilities={{ServiceType::Fire,{30,17},30,1.f}};
  cache=city.cache();
  EXPECT_DOUBLE_EQ(cache.departments[0].capacity,0);
  EXPECT_DOUBLE_EQ(cache.departments[0].served,0);
}

TEST(DepartmentCapacityTests, OrderIsDeterministicAndNoOverlappingSiteDoubleCountsDemand) {
  DepartmentCity city;
  city.add({3,9},1500);city.add({12,9},1000);city.add({18,9},1000);
  city.facilities.push_back({ServiceType::Fire,{18,7},30,0.5f});
  const auto first=city.cache();
  std::vector<Building> buildings;
  for (const auto& [id,b] : city.store.getBuildings()) { (void)id; buildings.push_back(b); }
  city.store.clear();
  std::reverse(buildings.begin(),buildings.end());
  for (const auto& b : buildings) city.store.upsertBuilding(b);
  std::reverse(city.facilities.begin(),city.facilities.end());
  const auto second=city.cache();
  EXPECT_EQ(first.departmentFactors,second.departmentFactors);
  EXPECT_DOUBLE_EQ(first.departments[0].served,3000);
  EXPECT_DOUBLE_EQ(second.departments[0].served,3000);
  EXPECT_DOUBLE_EQ(second.departments[0].served+second.departments[0].unserved,3500);
}

TEST(DepartmentCapacityTests, UnservedBuildingsLoseLocalResponseAndDensificationCoverage) {
  DepartmentCity city;
  city.add({3,9},2000);city.add({12,9},1000);
  city.facilities.push_back({ServiceType::Education,{3,7},30,1.f});
  city.facilities.push_back({ServiceType::Health,{3,7},30,1.f});
  const auto limited=city.cache();
  const auto unlimited=city.cache(false);
  EXPECT_GT(FireSystem::responseAt(city.roads,unlimited,{12,9}),0.f);
  EXPECT_FLOAT_EQ(FireSystem::responseAt(city.roads,limited,{12,9}),0.f);
  const auto a=BuildingDiagnostics::evaluate(city.map,city.roads,city.store,{12,9},{1,1,1,1},limited);
  const auto b=BuildingDiagnostics::evaluate(city.map,city.roads,city.store,{12,9},{1,1,1,1},unlimited);
  EXPECT_LT(a.growth.civicCoverage,b.growth.civicCoverage);
  EXPECT_DOUBLE_EQ(a.departmentServed[0],0);
  EXPECT_DOUBLE_EQ(a.departmentUnserved[0],1000);
}

TEST(DepartmentCapacityTests, SmallCoveredCityMatchesUnboundedCoverageResponseAndNeighborhoodRates) {
  DepartmentCity city;
  city.add({3,9},399);city.add({12,9},300,BuildingType::Commercial);
  city.facilities.push_back({ServiceType::Education,{3,7},30,1.f});
  city.facilities.push_back({ServiceType::Health,{3,7},30,1.f});
  const auto limited=city.cache();const auto unlimited=city.cache(false);
  const auto a=ServiceSystem::evaluateFromCache(city.store,city.roads,limited);
  const auto b=ServiceSystem::evaluateFromCache(city.store,city.roads,unlimited);
  EXPECT_FLOAT_EQ(a.fireCoverage,b.fireCoverage);
  EXPECT_FLOAT_EQ(a.policeCoverage,b.policeCoverage);
  EXPECT_FLOAT_EQ(a.sanitationCoverage,b.sanitationCoverage);
  EXPECT_FLOAT_EQ(a.satisfaction,b.satisfaction);
  EXPECT_FLOAT_EQ(FireSystem::responseAt(city.roads,limited,{3,9}),FireSystem::responseAt(city.roads,unlimited,{3,9}));
  const auto d=BuildingDiagnostics::evaluate(city.map,city.roads,city.store,{3,9},{1,1,1,1},limited);
  const auto e=BuildingDiagnostics::evaluate(city.map,city.roads,city.store,{3,9},{1,1,1,1},unlimited);
  EXPECT_FLOAT_EQ(d.growth.civicCoverage,e.growth.civicCoverage);
  const auto x=NeighborhoodSystem::evaluate(city.map,city.roads,city.store,limited,.1f,.5f,120.f,.1f);
  const auto y=NeighborhoodSystem::evaluate(city.map,city.roads,city.store,unlimited,.1f,.5f,120.f,.1f);
  EXPECT_FLOAT_EQ(x.illnessRate,y.illnessRate);EXPECT_FLOAT_EQ(x.crimeRate,y.crimeRate);
  EXPECT_TRUE(unlimited.departmentSites.empty());
}
