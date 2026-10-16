#include "src/systems/FreightSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
struct Stock {
  const Building *building;
  int64_t remaining;
};
std::vector<Stock> stocks(const CityMap &map, const EntityStore &store,
                          BuildingType type, double goodsPerWorker, bool requireWater) {
  std::vector<Stock> result;
  if (!std::isfinite(goodsPerWorker) || goodsPerWorker < 0.0)
    return result;
  for (auto id : store.idsByBuildingType(type)) {
    const auto *building = store.getBuilding(id);
    if (!map.getTile(building->position).connectedToPower ||
        (requireWater && !map.getTile(building->position).connectedToWater) ||
        building->occupancy <= 0)
      continue;
    result.push_back({building, 0});
  }
  std::sort(result.begin(), result.end(), [](const Stock &a, const Stock &b) {
    return a.building->position.x != b.building->position.x
               ? a.building->position.x < b.building->position.x
               : a.building->position.y < b.building->position.y;
  });
  uint64_t workers = 0;
  int64_t allocated = 0;
  for (auto &stock : result) {
    workers += static_cast<uint64_t>(stock.building->occupancy);
    const double cumulative = static_cast<double>(workers) * goodsPerWorker;
    const int64_t target =
        cumulative >= static_cast<double>(std::numeric_limits<int64_t>::max())
            ? std::numeric_limits<int64_t>::max()
            : static_cast<int64_t>(cumulative);
    stock.remaining = target - allocated;
    allocated = target;
  }
  result.erase(
      std::remove_if(result.begin(), result.end(),
                     [](const Stock &stock) { return stock.remaining == 0; }),
      result.end());
  return result;
}
} // namespace

FreightSummary FreightSystem::simulate(const CityMap &map, RoadNetwork &roads,
                                       const EntityStore &store,
                                       FreightRouteCache *cache,
                                       const TradeRates &rates,
                                       const std::vector<FreightGateway>* gateways) {
  FreightSummary summary;
  if (gateways) {
    if (gateways->size()>4097) throw std::invalid_argument("too many freight gateways");
    std::set<uint32_t> ids;
    for (const auto& gateway:*gateways) {
      if (!gateway.id || !ids.insert(gateway.id).second || gateway.goodsPerTick<0 ||
          !map.isValid(gateway.position))
        throw std::invalid_argument("invalid freight gateway");
      summary.gateways.push_back({gateway,0,0});
    }
    std::sort(summary.gateways.begin(),summary.gateways.end(),[](const auto& a,const auto& b) {
      return a.gateway.id<b.gateway.id;
    });
  }
  summary.gate = {0, map.getDimensions().y / 2};
  Coord gateAnchor;
  summary.gateConnected = roads.resolveRoadAnchor(summary.gate, gateAnchor);
  auto producers = stocks(map, store, BuildingType::Industrial,
                          rates.goodsPerIndustrialWorker,gateways!=nullptr);
  auto consumers = stocks(map, store, BuildingType::Commercial,
                          rates.goodsPerCommercialWorker,gateways!=nullptr);
  for (const auto &producer : producers)
    summary.goodsProduced += producer.remaining;
  for (const auto &consumer : consumers)
    summary.goodsConsumed += consumer.remaining;
  if (cache && (cache->topologyVersion != roads.getTopologyVersion() ||
                cache->storeVersion != store.getMutationVersion())) {
    cache->paths.clear();
    cache->topologyVersion = roads.getTopologyVersion();
    cache->storeVersion = store.getMutationVersion();
  }
  const auto ship = [&](Coord from, Coord to, int64_t goods,
                        int64_t &delivered, bool countFailure = true) {
    Coord origin, destination;
    if (!roads.resolveRoadAnchor(from, origin) ||
        !roads.resolveRoadAnchor(to, destination)) {
      if (countFailure) summary.unroutedGoods += goods;
      return false;
    }
    Pathfinding::Path path;
    const RoadNetwork::EdgeKey key{origin, destination};
    if (cache) {
      if (cache->paths.size() >= kMaxCachedRoutes && !cache->paths.count(key))
        cache->paths.clear();
      auto it = cache->paths.find(key);
      if (it == cache->paths.end())
        it = cache->paths
                 .emplace(key, Pathfinding::findShortestPath(roads, origin,
                                                             destination))
                 .first;
      path = it->second;
    } else {
      path = Pathfinding::findShortestPath(roads, origin, destination);
    }
    if (!path.found) {
      if (countFailure) summary.unroutedGoods += goods;
      return false;
    }
    delivered += goods;
    ++summary.routes;
    const uint64_t trucks = static_cast<uint64_t>(
        goods / kGoodsPerTruck + (goods % kGoodsPerTruck != 0));
    if (path.waypoints.size() > 1)
      summary.trucks += trucks;
    for (size_t i = 1; i < path.waypoints.size(); ++i)
      roads.addFreightLoad(path.waypoints[i - 1], path.waypoints[i],
                           static_cast<float>(trucks));
    return true;
  };
  const auto external = [&](Coord local,int64_t goods,bool importing,int64_t& delivered) {
    if (!gateways) {
      ship(importing ? summary.gate : local,importing ? local : summary.gate,goods,delivered);
      return;
    }
    int64_t remaining=goods;
    for (auto& usage:summary.gateways) {
      if (!remaining) break;
      if (!usage.gateway.enabled) continue;
      const int64_t available=usage.gateway.goodsPerTick-usage.importedGoods-usage.exportedGoods;
      const int64_t batch=std::min(remaining,available);
      if (!batch) continue;
      int64_t confirmed=0;
      if (!ship(importing ? usage.gateway.position : local,
                importing ? local : usage.gateway.position,batch,confirmed,false)) continue;
      remaining-=confirmed; delivered+=confirmed;
      if (importing) usage.importedGoods+=confirmed;
      else usage.exportedGoods+=confirmed;
    }
    summary.unroutedGoods+=remaining;
  };
  // Industrial sites receive outside restocking before forwarding goods to
  // retailers. Split the net deficit by capacity and proximity to the gate.
  const int64_t neededImports =
      std::max<int64_t>(0, summary.goodsConsumed - summary.goodsProduced);
  if (!producers.empty() && neededImports > 0) {
    std::vector<double> weights;
    double totalWeight = 0.0;
    for (const auto &producer : producers) {
      const auto position = producer.building->position;
      const double distance = std::abs(position.x - summary.gate.x) +
                              std::abs(position.y - summary.gate.y);
      weights.push_back(static_cast<double>(producer.building->capacity) /
                        (1.0 + distance));
      totalWeight += weights.back();
    }
    int64_t unassigned = neededImports;
    for (size_t i = 0; i < producers.size(); ++i) {
      const double share =
          static_cast<double>(neededImports) * weights[i] / totalWeight;
      const int64_t goods =
          i + 1 == producers.size() || share >= static_cast<double>(unassigned)
              ? unassigned
              : static_cast<int64_t>(share);
      unassigned -= goods;
      if (goods == 0)
        continue;
      int64_t delivered = 0;
      external(producers[i].building->position,goods,true,delivered);
      summary.importedGoods += delivered;
      producers[i].remaining += delivered;
    }
  }
  // Capacity/distance weighted destination choice; one shipment consumes a
  // producer or consumer's remaining stock, so no per-worker/per-truck loop.
  for (auto &producer : producers) {
    while (producer.remaining > 0) {
      Stock *best = nullptr;
      double bestWeight = -1.0;
      for (auto &consumer : consumers) {
        if (consumer.remaining <= 0)
          continue;
        const double distance = std::abs(producer.building->position.x -
                                         consumer.building->position.x) +
                                std::abs(producer.building->position.y -
                                         consumer.building->position.y);
        const double weight =
            static_cast<double>(consumer.building->capacity) / (1.0 + distance);
        if (weight > bestWeight) {
          bestWeight = weight;
          best = &consumer;
        }
      }
      if (!best)
        break;
      const int64_t goods = std::min(producer.remaining, best->remaining);
      ship(producer.building->position, best->building->position, goods,
           summary.domesticGoods);
      producer.remaining -= goods;
      best->remaining -= goods;
    }
    if (producer.remaining > 0)
      external(producer.building->position,producer.remaining,false,summary.exportedGoods);
  }
  // Imports restock commercial demand. Without an industrial base they still
  // travel gate -> retailer instead of disappearing from the road model.
  for (const auto &consumer : consumers)
    if (producers.empty() && consumer.remaining > 0)
      external(consumer.building->position,consumer.remaining,true,summary.importedGoods);
  return summary;
}
