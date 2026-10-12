#pragma once

#include <cstdint>
#include <unordered_map>

#include "src/entities/EntityStore.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/EconomySystem.hpp"

struct FreightSummary {
  int64_t goodsProduced = 0;
  int64_t goodsConsumed = 0;
  int64_t domesticGoods = 0;
  int64_t exportedGoods = 0;
  int64_t importedGoods = 0;
  int64_t unroutedGoods = 0;
  uint64_t trucks = 0;
  uint32_t routes = 0;
  Coord gate{0, 0};
  bool gateConnected = false;
};

// Bounded topology-only route reuse. Capacity upgrades do not change paths.
struct FreightRouteCache {
  uint64_t topologyVersion = static_cast<uint64_t>(-1);
  uint64_t storeVersion = static_cast<uint64_t>(-1);
  std::unordered_map<RoadNetwork::EdgeKey, Pathfinding::Path,
                     RoadNetwork::EdgeKeyHash>
      paths;
};

class FreightSystem {
public:
  static constexpr int64_t kGoodsPerTruck = 32;
  static constexpr size_t kMaxCachedRoutes = 128;
  // Add batched freight to existing commuter loads; never reset congestion.
  // The fixed west-edge gate must have road access for outside trade.
  static FreightSummary simulate(const CityMap &map, RoadNetwork &roads,
                                 const EntityStore &store,
                                 FreightRouteCache *cache = nullptr,
                                 const TradeRates &rates = {});
};
