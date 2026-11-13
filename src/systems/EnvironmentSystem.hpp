#pragma once
#include <vector>
#include "src/world/CityMap.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/systems/ServiceSystem.hpp"

class EnvironmentSystem {
public:
  static constexpr double cellArea=2500.; // 50m × 50m.
  static void initialize(CityMap& map,uint32_t seed);
  static void updateWind(CityMap& map,const EntityStore& store);
  static void step(CityMap& map,const RoadNetwork& roads,const EntityStore& store,
                   const std::vector<ServiceFacility>& fundedFacilities,uint32_t seed,bool transportAir=true);
  static void transportAir(CityMap& map);
  // Read-only projection used by previews and service caches.
  static float windPowerFactor(float speed);
  static float windPowerFactor(const CityMap& map,Coord position);
  static float cleanFraction(const CityMap& map,Coord position);
  static float drainageFactor(const CityMap& map,Coord position);
  static std::vector<float> waterCapacities(const CityMap& map,const std::vector<ServiceFacility>& facilities);
};
