#include "RegionalAccounting.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace regional {
namespace {
int64_t product(uint32_t units,int64_t price) {
  if (price<0 || (units && price>std::numeric_limits<int64_t>::max()/units))
    throw std::invalid_argument("regional amount overflow or negative price");
  return units*price;
}
int64_t sum(int64_t a,int64_t b) {
  if (a<0 || b<0 || a>std::numeric_limits<int64_t>::max()-b)
    throw std::invalid_argument("regional amount overflow or negative cost");
  return a+b;
}
void add(uint32_t& total,uint32_t value) {
  if (value>UINT32_MAX-total) throw std::invalid_argument("regional flow overflow");
  total+=value;
}
bool equal(Demand a,Demand b) {
  return a.passengers==b.passengers && a.imports==b.imports && a.exports==b.exports;
}
}

bool settle(const Report& report,const std::vector<Delivery>& deliveries,
            Prices prices,Settlement& previous,std::string* error) {
  if (error) error->clear();
  try {
    if (!report.tick || (previous.valid && report.tick<=previous.allocation.tick))
      throw std::invalid_argument("regional tick already finalized or invalid");
    if (report.connections.size()>4097 || deliveries.size()>4097)
      throw std::invalid_argument("too many regional flow rows");
    Settlement next; next.valid=true; next.allocation=report; next.prices=prices;
    std::sort(next.allocation.connections.begin(),next.allocation.connections.end(),
      [](const auto& a,const auto& b){return a.id<b.id;});
    std::set<uint32_t> ids;
    Demand admitted;
    for (const auto& u:report.connections) {
      if (!u.id || !ids.insert(u.id).second || static_cast<int>(u.access)<0 ||
          static_cast<int>(u.access)>static_cast<int>(Access::NotConstructed) || (u.access!=Access::Ready && (u.passengers || u.imports || u.exports)))
        throw std::invalid_argument("invalid regional allocation row");
      if ((!u.capacityMeasured && (u.availablePassengers || u.availableGoods)) ||
          (u.capacityMeasured && (u.passengers>u.availablePassengers ||
            static_cast<uint64_t>(u.imports)+u.exports>u.availableGoods ||
            (u.access!=Access::Ready && (u.availablePassengers || u.availableGoods)))))
        throw std::invalid_argument("regional flow exceeds available capacity");
      add(admitted.passengers,u.passengers); add(admitted.imports,u.imports); add(admitted.exports,u.exports);
    }
    if (!equal(admitted,report.admitted) || admitted.passengers>report.requested.passengers ||
        admitted.imports>report.requested.imports || admitted.exports>report.requested.exports ||
        !equal(report.unmet,{report.requested.passengers-admitted.passengers,
          report.requested.imports-admitted.imports,report.requested.exports-admitted.exports}))
      throw std::invalid_argument("regional allocation does not reconcile");
    std::set<uint32_t> confirmed;
    for (const auto& d:deliveries) {
      const auto found=std::lower_bound(next.allocation.connections.begin(),next.allocation.connections.end(),d.id,
        [](const auto& row,uint32_t id){return row.id<id;});
      if (found==next.allocation.connections.end() || found->id!=d.id || !confirmed.insert(d.id).second ||
          d.delivered.passengers>found->passengers || d.delivered.imports>found->imports ||
          d.delivered.exports>found->exports)
        throw std::invalid_argument("unallocated or duplicate regional delivery");
      add(next.delivered.passengers,d.delivered.passengers);
      add(next.delivered.imports,d.delivered.imports); add(next.delivered.exports,d.delivered.exports);
      next.deliveries.push_back(d);
    }
    std::sort(next.deliveries.begin(),next.deliveries.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    next.undelivered={admitted.passengers-next.delivered.passengers,
      admitted.imports-next.delivered.imports,admitted.exports-next.delivered.exports};
    next.revenue=sum(product(next.delivered.passengers,prices.passengerFee),
                     product(next.delivered.exports,prices.exportRevenue));
    next.expenses=sum(product(next.delivered.imports,prices.importCost),prices.operatingCost);
    previous=std::move(next);
    return true;
  } catch (const std::exception& e) {
    if (error) *error=e.what();
    return false;
  }
}
} // namespace regional
