#pragma once

#include <cstdint>
#include <vector>
#include "src/world/CityMap.hpp"

class RoadNetwork;

namespace regional {

enum class Kind { RoadGateway, CargoPort, PassengerTerminal, Airport };
enum class Access { Ready, Disabled, InvalidFootprint, Occupied, MissingEntrance,
                    NoRoadPath, MissingPower, MissingWater, NoSeaAccess, NotConstructed };

struct Connection {
  uint32_t id = 0;
  Kind kind = Kind::RoadGateway;
  Coord origin{0,0};
  int width = 1;
  int height = 1;
  Coord entrance{0,0}; // Road tile immediately outside the footprint.
  bool enabled = true;
  uint32_t passengersPerTick = 0;
  uint32_t goodsPerTick = 0; // Shared by imports and exports.
  bool constructed = false; // Legacy configuration remains dormant.
};

// Derived terrain data. Rebuild explicitly after terrain changes; allocation
// does not scan terrain. Four-neighbor water components must reach a map edge.
class SeaAccess {
public:
  explicit SeaAccess(const CityMap& map);
  bool reachesBoundary(Coord water) const;
private:
  int width_ = 0;
  int height_ = 0;
  std::vector<bool> reachable_;
};

// Pure preflight: does not reserve tiles, spend money or alter any city state.
Access diagnoseSite(const Connection& connection, const CityMap& map,
                    const RoadNetwork& roads, const SeaAccess& sea,
                    Coord destinationRoad,
                    const std::vector<Connection>& reservations = {});
// Operational checks require every footprint tile to belong to this built
// connection. Unlike placement preflight, its own reservation is expected.
// Readiness at the entrance does not promise access to every local destination:
// pass a destination to test a particular city-side route, or the entrance itself
// to inspect the site. No external road gateway is required for a maritime port.
Access diagnoseOperation(const Connection& connection, const CityMap& map,
                         const RoadNetwork& roads, const SeaAccess& sea,
                         Coord destinationRoad);
const char* accessName(Access access);

struct Offer { Connection connection; Access access = Access::Ready; };
struct Demand { uint32_t passengers = 0, imports = 0, exports = 0; };
struct Usage {
  uint32_t id = 0;
  Access access = Access::Ready;
  uint32_t passengers = 0, imports = 0, exports = 0;
  bool capacityMeasured = false; // Legacy reports omit available capacity.
  uint32_t availablePassengers = 0, availableGoods = 0;
};
struct Report {
  uint32_t tick = 0;
  Demand requested, admitted, unmet;
  std::vector<Usage> connections;
};

// Stable ascending-ID allocation, imports before exports in shared cargo
// capacity. Admission is not delivery or a trade/treasury credit. A later local
// freight leg must confirm delivery before M32 books fulfilled trade.
Report allocate(uint32_t tick, Demand demand, std::vector<Offer> offers);

} // namespace regional
