#include "RegionalLogistics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace regional {
namespace {
uint32_t units(int64_t value) {
  if (value<0 || value>UINT32_MAX) throw std::overflow_error("regional flow outside report range");
  return static_cast<uint32_t>(value);
}
int64_t price(int64_t quantity, double rate) {
  const double value=static_cast<double>(quantity)*rate;
  if (quantity<0 || !std::isfinite(value) || value<0 ||
      value>=static_cast<double>(std::numeric_limits<int64_t>::max()))
    throw std::overflow_error("regional price outside economy range");
  return static_cast<int64_t>(value);
}
}
bool hasConstructedCargoPort(const std::vector<Connection>& connections) {
  return std::any_of(connections.begin(),connections.end(),[](const auto& c) {
    return c.constructed && c.kind==Kind::CargoPort;
  });
}
int64_t portOperatingCost(const std::vector<Connection>& connections) {
  int64_t result=0;
  for (const auto& c:connections) {
    if (!c.constructed || !c.enabled) continue;
    if (c.kind==Kind::CargoPort) result+=kCargoUpkeep;
    if (c.kind==Kind::PassengerTerminal) result+=kPassengerUpkeep;
  }
  return result;
}
LogisticsPlan planLogistics(const CityMap& map, const RoadNetwork& roads,
                            const std::vector<Connection>& connections) {
  if (connections.size()>4096) throw std::invalid_argument("too many regional connections");
  std::set<uint32_t> ids;
  for (const auto& c:connections)
    if (!c.id || !ids.insert(c.id).second) throw std::invalid_argument("invalid regional connection ID");
  uint32_t roadId=UINT32_MAX;
  while (ids.count(roadId)) --roadId;
  Connection road; road.id=roadId; road.goodsPerTick=kRoadGoodsPerTick;
  road.origin=road.entrance={0,map.getDimensions().y/2};
  const auto roadAccess=roads.hasRoadAdjacency(road.entrance) ? Access::Ready : Access::MissingEntrance;
  LogisticsPlan result;
  result.offers.push_back({road,roadAccess});
  result.gateways.push_back({roadId,road.entrance,kRoadGoodsPerTick,roadAccess==Access::Ready});
  const SeaAccess sea(map);
  for (const auto& c:connections) {
    const auto access=diagnoseOperation(c,map,roads,sea,c.entrance);
    result.offers.push_back({c,access});
    if (c.constructed && c.kind==Kind::CargoPort)
      result.gateways.push_back({c.id,c.entrance,c.goodsPerTick,access==Access::Ready});
  }
  std::sort(result.offers.begin(),result.offers.end(),[](const auto& a,const auto& b) {
    return a.connection.id<b.connection.id;
  });
  std::sort(result.gateways.begin(),result.gateways.end(),[](const auto& a,const auto& b) {
    return a.id<b.id;
  });
  return result;
}
void applyFulfilledTrade(EconomyState& economy,const FreightSummary& freight,const TradeRates& rates) {
  economy.exportRevenue=price(freight.exportedGoods,static_cast<double>(rates.exportPricePerUnit)*economy.inflationMultiplier);
  economy.importCost=price(freight.importedGoods,static_cast<double>(rates.importCostPerUnit)*economy.inflationMultiplier);
  const auto retailDelivery=freight.goodsProduced>0 ? freight.domesticGoods : freight.importedGoods;
  economy.goodsAvailability=freight.goodsConsumed>0 ? static_cast<float>(std::clamp(
    static_cast<double>(retailDelivery)/freight.goodsConsumed,0.0,1.0)) : 1.0f;
  const auto oldCommercialTax=economy.commercialTaxRevenue;
  economy.commercialTaxRevenue=static_cast<int64_t>(oldCommercialTax*static_cast<double>(economy.goodsAvailability));
  economy.totalTaxRevenue+=economy.commercialTaxRevenue-oldCommercialTax;
  economy.totalRevenue=economy.totalTaxRevenue+economy.exportRevenue;
  economy.totalExpenses=economy.totalMaintenance+economy.importCost;
  economy.balance=economy.totalRevenue-economy.totalExpenses;
  economy.economicHealth*=economy.goodsAvailability;
}
Settlement measureLogistics(uint32_t tick,const LogisticsPlan& plan,const FreightSummary& freight,
                             double amountScale,int64_t operatingCost) {
  Report report; report.tick=tick;
  report.requested.imports=units(std::max<int64_t>(0,freight.goodsConsumed-freight.goodsProduced));
  report.requested.exports=units(std::max<int64_t>(0,freight.goodsProduced-freight.goodsConsumed));
  std::vector<Delivery> deliveries;
  for (const auto& offer:plan.offers) {
    Usage row; row.id=offer.connection.id; row.access=offer.access;
    const auto found=std::find_if(freight.gateways.begin(),freight.gateways.end(),[&](const auto& u) {
      return u.gateway.id==row.id;
    });
    if (found!=freight.gateways.end()) {
      row.imports=units(found->importedGoods); row.exports=units(found->exportedGoods);
      if (static_cast<uint64_t>(row.imports)+row.exports>offer.connection.goodsPerTick)
        throw std::invalid_argument("regional delivery exceeds capacity");
    }
    report.connections.push_back(row);
    if (row.imports || row.exports) deliveries.push_back({row.id,{0,row.imports,row.exports}});
  }
  report.admitted={0,units(freight.importedGoods),units(freight.exportedGoods)};
  if (report.admitted.imports>report.requested.imports || report.admitted.exports>report.requested.exports)
    throw std::invalid_argument("regional delivery exceeds external demand");
  report.unmet={0,report.requested.imports-report.admitted.imports,report.requested.exports-report.admitted.exports};
  Settlement result; std::string error;
  // Upkeep is in city dollars, trade prices in the economy's raw units.
  // Preserve both explicitly rather than rounding each unit before billing.
  if (!settle(report,deliveries,{0,60,40,0},result,&error)) throw std::invalid_argument(error);
  if (!std::isfinite(amountScale) || amountScale<0 || operatingCost<0)
    throw std::invalid_argument("invalid regional monetary scale or upkeep");
  result.amountScale=amountScale;
  result.operatingExpense=operatingCost;
  return result;
}
} // namespace regional
