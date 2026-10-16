#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <limits>
#include <cmath>
#include "src/systems/RegionalAccounting.hpp"

inline nlohmann::json regionalDemandJson(regional::Demand d) {
  return {{"passengers",d.passengers},{"imports",d.imports},{"exports",d.exports}};
}
inline nlohmann::json regionalSettlementJson(const regional::Settlement& s) {
  auto rows=nlohmann::json::array(), deliveries=nlohmann::json::array();
  for (const auto& u:s.allocation.connections) {
    rows.push_back({{"id",u.id},{"access",static_cast<int>(u.access)},
      {"passengers",u.passengers},{"imports",u.imports},{"exports",u.exports}});
    if (u.capacityMeasured) {
      rows.back()["availablePassengers"]=u.availablePassengers;
      rows.back()["availableGoods"]=u.availableGoods;
    }
  }
  for (const auto& d:s.deliveries)
    deliveries.push_back({{"id",d.id},{"delivered",regionalDemandJson(d.delivered)}});
  nlohmann::json result={{"valid",s.valid},{"allocation",{{"tick",s.allocation.tick},
    {"requested",regionalDemandJson(s.allocation.requested)},
    {"admitted",regionalDemandJson(s.allocation.admitted)},
    {"unmet",regionalDemandJson(s.allocation.unmet)},{"connections",rows}}},
    {"deliveries",deliveries},{"delivered",regionalDemandJson(s.delivered)},
    {"undelivered",regionalDemandJson(s.undelivered)},
    {"prices",{{"passengerFee",s.prices.passengerFee},{"importCost",s.prices.importCost},
      {"exportRevenue",s.prices.exportRevenue},{"operatingCost",s.prices.operatingCost}}},
    {"revenue",s.revenue},{"expenses",s.expenses}};
  if (s.amountScale!=1.0 || s.operatingExpense!=0) {
    result["amountScale"]=s.amountScale;
    result["operatingExpense"]=s.operatingExpense;
  }
  return result;
}

inline regional::Settlement parseRegionalSettlement(const nlohmann::json& data,uint32_t currentTick) {
  auto number=[](const nlohmann::json& value,uint64_t maximum) -> uint64_t {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>()<0))
      throw std::runtime_error("Invalid regional settlement integer");
    const auto n=value.get<uint64_t>();
    if (n>maximum) throw std::runtime_error("Regional settlement integer out of range");
    return n;
  };
  auto u32=[&](const nlohmann::json& v) {return static_cast<uint32_t>(number(v,UINT32_MAX));};
  auto amount=[&](const nlohmann::json& v) {return static_cast<int64_t>(number(v,std::numeric_limits<int64_t>::max()));};
  auto demand=[&](const nlohmann::json& d) -> regional::Demand {
    return {u32(d.at("passengers")),u32(d.at("imports")),u32(d.at("exports"))};
  };
  if (!data.at("valid").is_boolean()) throw std::runtime_error("Invalid regional settlement validity");
  if (!data.at("valid").get<bool>()) {
    if (data!=regionalSettlementJson({})) throw std::runtime_error("Inconsistent unmeasured regional settlement");
    return {};
  }
  const auto& a=data.at("allocation");
  regional::Report report;
  report.tick=u32(a.at("tick"));
  if (!report.tick || report.tick>currentTick) throw std::runtime_error("Invalid regional settlement tick");
  report.requested=demand(a.at("requested")); report.admitted=demand(a.at("admitted"));
  report.unmet=demand(a.at("unmet"));
  const auto& rows=a.at("connections"); const auto& confirmations=data.at("deliveries");
  if (!rows.is_array() || rows.size()>4097 || !confirmations.is_array() || confirmations.size()>4097)
    throw std::runtime_error("Invalid regional settlement rows");
  for (const auto& row:rows) {
    report.connections.push_back({u32(row.at("id")),static_cast<regional::Access>(number(row.at("access"),static_cast<int>(regional::Access::NotConstructed))),
      u32(row.at("passengers")),u32(row.at("imports")),u32(row.at("exports"))});
    if (row.contains("availablePassengers") || row.contains("availableGoods")) {
      auto& u=report.connections.back();u.capacityMeasured=true;
      u.availablePassengers=u32(row.at("availablePassengers"));u.availableGoods=u32(row.at("availableGoods"));
    }
  }
  std::vector<regional::Delivery> deliveries;
  for (const auto& row:confirmations) deliveries.push_back({u32(row.at("id")),demand(row.at("delivered"))});
  const auto& p=data.at("prices");
  regional::Prices prices{amount(p.at("passengerFee")),amount(p.at("importCost")),
    amount(p.at("exportRevenue")),amount(p.at("operatingCost"))};
  regional::Settlement result; std::string error;
  if (!regional::settle(report,deliveries,prices,result,&error)) throw std::runtime_error(error);
  if (result.revenue!=amount(data.at("revenue")) || result.expenses!=amount(data.at("expenses")) ||
      regionalDemandJson(result.delivered)!=regionalDemandJson(demand(data.at("delivered"))) ||
      regionalDemandJson(result.undelivered)!=regionalDemandJson(demand(data.at("undelivered"))))
    throw std::runtime_error("Regional settlement totals do not reconcile");
  if (data.contains("amountScale")) {
    if (!data.at("amountScale").is_number()) throw std::runtime_error("Invalid regional amount scale");
    result.amountScale=data.at("amountScale").get<double>();
    if (!std::isfinite(result.amountScale) || result.amountScale<0)
      throw std::runtime_error("Invalid regional amount scale");
  }
  if (data.contains("operatingExpense")) result.operatingExpense=amount(data.at("operatingExpense"));
  return result;
}
