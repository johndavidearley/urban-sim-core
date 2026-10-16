#include "TourismAccess.hpp"
#include <algorithm>
#include <queue>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/ParkSystem.hpp"
#include "src/systems/RegionalLogistics.hpp"

namespace tourism {
void reconcileRemovedHotels(State& state, const EntityStore& buildings) {
  std::vector<Stay> retained;
  uint64_t pending = state.pendingEarlyDepartures;
  for (const auto& stay : state.stays) {
    const auto* building = buildings.getBuilding(stay.hotelId);
    if (!building || building->type != BuildingType::Commercial || !building->hotelBeds)
      pending += stay.guests;
    else retained.push_back(stay);
  }
  if (pending > kMaximumDemand / 4 * kStayTicks)
    throw std::invalid_argument("too many pending tourism departures");
  state.stays = std::move(retained);
  state.pendingEarlyDepartures = static_cast<uint32_t>(pending);
}
AccessPlan planAccess(const CityMap& map, const RoadNetwork& roads,
    const EntityStore& buildings, const std::vector<regional::Connection>& connections,
    uint32_t qualityPercent) {
  // Validate the bounded score even when the city has no parks.
  (void)demandFor({0,qualityPercent});
  AccessPlan result; result.attractions.qualityPercent = qualityPercent;
  const auto logistics = regional::planLogistics(map,roads,connections);
  uint32_t roadId = UINT32_MAX;
  // planLogistics sorts offers, so derive the synthetic ID without relying on
  // the order of its cargo gateway list.
  while (std::any_of(connections.begin(),connections.end(),
      [&](const auto& c) { return c.id == roadId; })) --roadId;
  // Label only queried road components, once each. Reachability does not need
  // one A* search per gateway/park/hotel combination.
  std::unordered_map<Coord,uint32_t,Vec2Hash> components;
  uint32_t nextComponent = 0;
  auto component = [&](Coord anchor) {
    if (!roads.hasRoadAdjacency(anchor)) return uint32_t{0};
    const auto found = components.find(anchor);
    if (found != components.end()) return found->second;
    const auto label = ++nextComponent;
    std::queue<Coord> pending; pending.push(anchor); components.emplace(anchor,label);
    while (!pending.empty()) {
      const auto coord = pending.front(); pending.pop();
      const auto* node = roads.getNode(coord);
      if (!node) continue;
      for (const auto& neighbor : node->adjacent)
        if (components.emplace(neighbor.coord,label).second) pending.push(neighbor.coord);
    }
    return label;
  };
  std::set<uint32_t> gatewayComponents;
  for (auto offer : logistics.offers) {
    auto& c = offer.connection;
    // The synthetic west road is the only automatic road visitor gateway.
    // Configured, unconstructed connection records remain dormant.
    if (c.kind == regional::Kind::CargoPort ||
        (c.id != roadId && !c.constructed)) continue;
    if (c.kind == regional::Kind::RoadGateway && !c.constructed)
      c.passengersPerTick = kRoadPassengersPerTick;
    result.offers.push_back(offer);
    // Potential attraction demand follows the local road component, even when
    // this facility is closed, unpowered or set to zero capacity. Operational
    // readiness still gates admissions, so a closure remains visible as unmet
    // transport rather than erasing otherwise reachable attraction demand.
    const auto label=component(c.entrance);
    if (label) gatewayComponents.insert(label);
  }
  std::set<uint32_t> attractionComponents;
  std::map<uint32_t,Coord> firstPark;
  const auto dimensions = map.getDimensions();
  for (int y=0;y<dimensions.y;++y) for (int x=0;x<dimensions.x;++x) {
    const Coord position{x,y}; Coord anchor;
    if (!ParkSystem::isPark(map,position) || !roads.resolveRoadAnchor(position,anchor)) continue;
    const auto label = component(anchor);
    if (label) firstPark.emplace(label,anchor);
    if (label && gatewayComponents.count(label)) {
      ++result.attractions.accessibleParks;
      attractionComponents.insert(label);
    }
  }
  for (auto& offer : result.offers) {
    if (offer.access == regional::Access::Ready &&
        !attractionComponents.count(component(offer.connection.entrance)))
      offer.access = regional::Access::NoRoadPath;
    const auto park=firstPark.find(component(offer.connection.entrance));
    if (park!=firstPark.end()) result.destinations.push_back({offer.connection.id,
      offer.connection.entrance,park->second,offer.connection.enabled && offer.access==regional::Access::Ready});
  }
  for (auto id : buildings.idsByBuildingType(BuildingType::Commercial)) {
    const auto* building = buildings.getBuilding(id);
    if (!building || !building->hotelBeds) continue;
    Hotel hotel; hotel.id = id; hotel.beds = building->hotelBeds;
    Coord anchor{-1,-1};
    const bool hasAnchor=map.isValid(building->position) && roads.resolveRoadAnchor(building->position,anchor);
    hotel.roadAnchor=anchor;
    const auto park=firstPark.find(component(anchor));
    if (park!=firstPark.end()) hotel.attractionRoad=park->second;
    hotel.enabled = building->hotelOpen && map.isValid(building->position) &&
      map.getTile(building->position).buildingId == id &&
      map.getTile(building->position).connectedToPower &&
      map.getTile(building->position).connectedToWater &&
      hasAnchor;
    if (hotel.enabled) {
      const auto label = component(anchor);
      for (const auto& offer : result.offers)
        if (offer.connection.enabled && offer.access == regional::Access::Ready &&
            component(offer.connection.entrance) == label)
          hotel.reachableConnections.push_back(offer.connection.id);
    }
    result.hotels.push_back(std::move(hotel));
  }
  return result;
}
}
