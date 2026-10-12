#include "src/systems/FreightSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
struct Stock {
  const Building *building;
  int64_t remaining;
};
std::vector<Stock> stocks(const CityMap &map, const EntityStore &store,
                          BuildingType type, double goodsPerWorker) {
  std::vector<Stock> result;
  if (!std::isfinite(goodsPerWorker) || goodsPerWorker < 0.0)
    return result;
  for (auto id : store.idsByBuildingType(type)) {
    const auto *building = store.getBuilding(id);
    if (!map.getTile(building->position).connectedToPower ||
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
                                       const TradeRates &rates) {
  FreightSummary summary;
  summary.gate = {0, map.getDimensions().y / 2};
  Coord gateAnchor;
  summary.gateConnected = roads.resolveRoadAnchor(summary.gate, gateAnchor);
  auto producers = stocks(map, store, BuildingType::Industrial,
                          rates.goodsPerIndustrialWorker);
  auto consumers = stocks(map, store, BuildingType::Commercial,
                          rates.goodsPerCommercialWorker);
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
                        int64_t &delivered) {
    Coord origin, destination;
    if (!roads.resolveRoadAnchor(from, origin) ||
        !roads.resolveRoadAnchor(to, destination)) {
      summary.unroutedGoods += goods;
      return;
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
      summary.unroutedGoods += goods;
      return;
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
      ship(summary.gate, producers[i].building->position, goods, delivered);
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
      ship(producer.building->position, summary.gate, producer.remaining,
           summary.exportedGoods);
  }
  // Imports restock commercial demand. Without an industrial base they still
  // travel gate -> retailer instead of disappearing from the road model.
  for (const auto &consumer : consumers)
    if (producers.empty() && consumer.remaining > 0)
      ship(summary.gate, consumer.building->position, consumer.remaining,
           summary.importedGoods);
  return summary;
}
