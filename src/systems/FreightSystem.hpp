#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/EconomySystem.hpp"

struct FreightGateway {
  uint32_t id = 0;
  Coord position{0,0};
  int64_t goodsPerTick = 0; // Imports and exports share this capacity.
  bool enabled = true;
};

struct FreightGatewayUsage {
  FreightGateway gateway;
  int64_t importedGoods = 0;
  int64_t exportedGoods = 0;
};

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
  std::vector<FreightGatewayUsage> gateways;
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
                                 const TradeRates &rates = {},
                                 const std::vector<FreightGateway>* gateways = nullptr);
  // A null gateway override preserves legacy fixed-gate behavior. An explicit
  // list, including an empty one, uses only those gateways in ascending ID order.
  // Capacity is consumed only by delivered shipments; unreachable destinations
  // may try another gateway without duplicate loads or unrouted accounting.
};
