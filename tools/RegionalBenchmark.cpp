// Optional local evidence tool. No hardware-dependent CTest timing thresholds.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include "src/gameplay/RegionalDiagnostics.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/persistence/ReplayVerifier.hpp"
#include "src/persistence/TourismJson.hpp"
#include "src/persistence/RegionalSettlementJson.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/systems/TourismAccess.hpp"

namespace {
using Clock=std::chrono::steady_clock;
using Json=nlohmann::json;
double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
void require(bool valid,const char* message) {
  if (!valid) throw std::runtime_error(message);
}
Json distribution(std::vector<double> values) {
  std::sort(values.begin(),values.end());
  return {{"mean_ms",std::accumulate(values.begin(),values.end(),0.0)/static_cast<double>(values.size())},
    {"median_ms",values[(values.size()-1)/2]},
    {"p95_ms",values[(values.size()-1)*95/100]},{"max_ms",values.back()}};
}
}
int main(int argc,char** argv) {
  try {
    if (argc<4 || argc>5) throw std::runtime_error("Usage: regional-benchmark SESSION TICKS OUTPUT.json [warm|cold]");
    const int ticks=std::stoi(argv[2]);
    require(ticks>0 && ticks<=100000,"Ticks must be 1..100000");
    const std::string mode=argc==5 ? argv[4] : "warm";
    require(mode=="warm" || mode=="cold","Mode must be warm or cold");
    int width=0,height=0;std::string error;
    if (!GameplaySessionSystem::readMapDimensions(argv[1],width,height,&error)) throw std::runtime_error(error);
    CityMap map{{width,height}};RoadNetwork roads{map};EntityStore store;PopulationStore people;
    GameplaySessionState session;
    if (!GameplaySessionSystem::load(argv[1],map,roads,store,people,session,&error)) throw std::runtime_error(error);
    // This profiler measures player-owned construction, not G-mode expansion.
    require(!session.autonomousGrowth,"Disable autonomous growth before profiling");
    DistrictSystem districts;
    for (const auto& record:session.districts) {
      const auto id=districts.createDistrict(record.name.empty() ? "DISTRICT" : record.name,
        {record.x1,record.y1},{record.x2,record.y2});
      districts.setDistrictArchetype(id,static_cast<DistrictArchetype>(record.archetype));
      districts.setDistrictServiceBudgetCap(id,record.serviceBudgetCap);
    }
    PlayableCityTickOptions options;
    options.baseSeed=1000u+session.simulationSeed-98765u;
    options.enableDisasters=session.enableDisasters;options.districts=&districts;
    PlayableCityTickState state;
    Json rows=Json::array();std::vector<double> durations,accessDurations;
    const auto initialTick=session.tick;
    for (int i=0;i<ticks;++i) {
      const uint64_t previousGuests=i==0 ? session.tourism.report.staying : state.tourism.report.staying;
      const auto started=Clock::now();
      if (i==0 || mode=="cold") state=GameplaySessionSystem::advance(map,roads,store,people,session);
      else playableCityTick(map,roads,store,people,session.facilities,state,session.funds,options);
      const double tickMs=milliseconds(started);durations.push_back(tickMs);
      const auto& r=state.tourism.report;
      require(r.dayArrivals+r.unmetDayTransport==r.requested.day,"Day flow mismatch");
      require(r.overnightArrivals+r.unmetOvernightBeds+r.unmetOvernightTransport==r.requested.overnight,"Overnight flow mismatch");
      require(previousGuests+r.overnightArrivals==static_cast<uint64_t>(r.staying)+r.scheduledDepartures+r.earlyDepartures,"Cohort mismatch");
      for (const auto& route:state.tourism.travel.routes)
        require(static_cast<uint64_t>(route.residentRiders)+route.ridership<=route.capacity,"Transit over capacity");
      const auto& ledger=state.treasuryLedger;
      require(ledger.breakdown.tourismFees==state.tourism.accounting.fees(),"Visitor fee mismatch");
      require(ledger.breakdown.tourismServices==state.tourism.accounting.services,"Visitor service mismatch");
      require(session.funds==ledger.fundsAfter,"Funds mismatch");
      require(ledger.fundsAfter-ledger.fundsBefore==ledger.flow.net,"Cash flow mismatch");
      require(std::accumulate(ledger.breakdown.revenue.begin(),ledger.breakdown.revenue.end(),int64_t{0})+
        ledger.breakdown.tourismFees==ledger.breakdown.totalRevenue,"Revenue categories mismatch");
      require(std::accumulate(ledger.breakdown.expenses.begin(),ledger.breakdown.expenses.end(),int64_t{0})+
        ledger.breakdown.tourismServices==ledger.breakdown.totalExpenses,"Expense categories mismatch");
      const auto& cargo=state.regionalSettlement;
      if (cargo.valid) {
        require(static_cast<uint64_t>(cargo.delivered.imports)+cargo.undelivered.imports==cargo.allocation.admitted.imports,"Import delivery mismatch");
        require(static_cast<uint64_t>(cargo.delivered.exports)+cargo.undelivered.exports==cargo.allocation.admitted.exports,"Export delivery mismatch");
      }
      require(state.freightRouteCache.paths.size()<=FreightSystem::kMaxCachedRoutes,"Freight cache exceeds bound");
      // Read-only access-plan micro-measurement is separate from the tick timer.
      // Count offers/hotels, not cache hits: no hit counter exists in this model.
      const auto accessStarted=Clock::now();
      const auto access=tourism::planAccess(map,roads,store,state.regionalConnections,r.attractions.qualityPercent);
      const double accessMs=milliseconds(accessStarted);accessDurations.push_back(accessMs);
      size_t serviceNodes=0,transitNodes=0;
      for (const auto& entry:state.serviceCache.entries) serviceNodes+=entry.distanceField.size();
      for (const auto& entry:state.transitCache.entries) transitNodes+=entry.distanceField.size();
      rows.push_back({{"tick",state.tick},{"tick_ms",tickMs},{"access_plan_ms",accessMs},
        {"population",people.getTotalPopulation()},{"buildings",store.getBuildingCount()},
        {"road_edges",roads.getRoadCount()},{"freight_routes",state.trafficSummary.freight.routes},
        {"freight_cache_paths",state.freightRouteCache.paths.size()},
        {"service_cache_distance_entries",serviceNodes},{"transit_cache_distance_entries",transitNodes},
        {"access_offers",access.offers.size()},{"hotels",access.hotels.size()},
        {"day",r.dayArrivals},{"staying",r.staying},{"funds",session.funds},
        {"alerts",regional::diagnose(cargo,state.tourism).size()}});
    }
#if defined(__clang__)
    const char* compiler="Clang " __clang_version__;
#elif defined(__GNUC__)
    const char* compiler="GCC " __VERSION__;
#elif defined(_MSC_VER)
    const char* compiler="MSVC";
#else
    const char* compiler="unknown";
#endif
    Json result={{"compiler",compiler},{"build_config",URBAN_SIM_PROFILE_CONFIG},{"mode",mode},{"width",width},{"height",height},{"ticks",ticks},
      {"initial_tick",initialTick},{"tick",distribution(durations)},
      {"access_plan",distribution(accessDurations)},{"samples",rows},
      {"final_world_checksum",ReplayVerifier::calculateChecksum(map,roads,store,people)},
      {"final_funds",session.funds},{"final_tourism",tourismJson(state.tourism,state.tick)},
      {"final_cargo",regionalSettlementJson(state.regionalSettlement)},
      {"note","Tick timing excludes load/save/render/inspection. Cold rebuilds host caches each advance; warm retains tick caches after its first tick. Access plan timing is a separate read-only probe, not additive overhead or a cache-hit metric."}};
    std::ofstream output(argv[3]);require(static_cast<bool>(output),"Cannot open output");
    output<<result.dump(2)<<'\n';require(static_cast<bool>(output),"Cannot write output");
    std::cout<<Json{{"mode",mode},{"tick",result["tick"]},{"access_plan",result["access_plan"]},
      {"final_sample",rows.back()}}.dump()<<'\n';
    return 0;
  } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
