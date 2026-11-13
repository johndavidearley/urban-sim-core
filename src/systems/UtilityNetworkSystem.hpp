#pragma once
#include "src/systems/ServiceSystem.hpp"
#include "src/entities/EntityStore.hpp"

struct UtilityNetworkResult {
  std::vector<double> sourceUsed,outletUsed,liftUsed,boosterUsed;
  std::vector<float> demand,delivered,handled,waterPressure;
  std::vector<bool> waterConnected,sewerConnected,liftPowered,boosterPowered;
  std::vector<double> backlog;
  double generated=0,discharged=0,totalBacklog=0;
};
class UtilityNetworkSystem {
public:
  static constexpr double pipeCapacity=100; // m3/tick per shared trunk tile.
  static constexpr double liftCapacity=50;
  static constexpr int64_t liftUpkeep=4;
  static constexpr double boosterCapacity=75;
  static constexpr int64_t boosterUpkeep=5;
  static int64_t operatingCost(const CityMap& map);
  static constexpr float pumpHead=20; // metres above the source's terrain.
  static UtilityNetworkResult evaluate(const CityMap& map,const EntityStore& store,
    const std::vector<ServiceFacility>& facilities);
  static void refresh(CityMap& map,const EntityStore& store,const std::vector<ServiceFacility>& facilities);
  static void applyDiagnostics(CityMap& map,const UtilityNetworkResult& result);
};
