#pragma once
#include "TourismAccess.hpp"
#include "TransitSystem.hpp"

namespace tourism {
// Plan aggregate local legs from the completed admissions and cohort changes.
// Day visits and hotel attraction visits are round trips. Overnight admissions
// and departures contribute one access/egress leg. Removed origins are explicit
// unrouted departures, never traffic invented from an invalid hotel reference.
std::vector<TravelLeg> planTravel(const State& previous,const State& current,
  const Report& report,const AccessPlan& access,const RoadNetwork& roads);

// Adds only visitor road load. Resident route usage is immutable and reserved
// first; visitor legs share remaining route capacity. Input plans are bounded.
TravelReport applyTravel(uint32_t tick,std::vector<TravelLeg> legs,RoadNetwork& roads,
  const std::vector<TransitRoute>& routes,const TransitCoverageCache& cache,
  const std::vector<TransitRouteUsage>& residents);
}
