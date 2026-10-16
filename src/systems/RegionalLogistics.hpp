#pragma once
#include "src/systems/FreightSystem.hpp"
#include "src/systems/RegionalAccounting.hpp"

namespace regional {
constexpr uint32_t kRoadGoodsPerTick = 100;
constexpr int64_t kCargoUpkeep = 20;
constexpr int64_t kPassengerUpkeep = 10;
struct LogisticsPlan {
  std::vector<Offer> offers;
  std::vector<FreightGateway> gateways;
};
// Includes a finite west-edge road gateway and operational constructed ports.
// Configured road/airport records stay dormant. The synthetic road ID is the
// highest ID unused by the registry, avoiding collision with saved IDs.
LogisticsPlan planLogistics(const CityMap& map, const RoadNetwork& roads,
                            const std::vector<Connection>& connections);
bool hasConstructedCargoPort(const std::vector<Connection>& connections);
int64_t portOperatingCost(const std::vector<Connection>& connections);
// Replaces forecast trade with fulfilled shipments. Commercial tax receipts and
// economic health scale with goods actually reaching retailers. Call on a fresh
// economy calculation, never cumulatively on an already adjusted economy.
void applyFulfilledTrade(EconomyState& economy, const FreightSummary& freight,
                         const TradeRates& rates = {});
// Local routing confirms admission and delivery together; unreachable candidates
// consume neither capacity nor admission. Monetary amounts use raw economy units
// and amountScale states their conversion to city dollars. Treasury books the
// adjusted economy and upkeep once; this report never deposits cash.
Settlement measureLogistics(uint32_t tick, const LogisticsPlan& plan,
                             const FreightSummary& freight, double amountScale,
                             int64_t operatingCost);
} // namespace regional
