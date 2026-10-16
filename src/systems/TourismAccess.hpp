#pragma once
#include "TourismSystem.hpp"

class EntityStore;
class RoadNetwork;
namespace tourism {
constexpr uint32_t kRoadPassengersPerTick = 100;
struct Destination {
  uint32_t gatewayId = 0;
  Coord gateway{-1,-1}, attraction{-1,-1};
  bool operating = false;
};
struct AccessPlan {
  AttractionInputs attractions;
  std::vector<regional::Offer> offers;
  std::vector<Hotel> hotels;
  std::vector<Destination> destinations;
};
// Rebuild from current world topology. Does not alter traffic, money or entities.
// Quality is supplied by the host; park and hotel accessibility are measured.
// Release hotel references removed by a paused edit, retaining a bounded count
// to book as early departures on the next completed tick.
void reconcileRemovedHotels(State& state, const EntityStore& buildings);
AccessPlan planAccess(const CityMap& map, const RoadNetwork& roads,
  const EntityStore& buildings, const std::vector<regional::Connection>& connections,
  uint32_t qualityPercent);
}
