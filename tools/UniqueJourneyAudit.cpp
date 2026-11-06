#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

// Reuse historical native construction checkpoints, carrying only the new
// achievement counters forward. M54 owns the new construction/budget journey.
int main(int argc,char** argv) {
  try {
    if(argc!=3) throw std::runtime_error("Usage: unique-journey-audit segments.json ticks.csv");
    std::ifstream input(argv[1]);if(!input) throw std::runtime_error("Cannot read manifest");
    const auto segments=nlohmann::json::parse(input);
    std::ofstream csv(argv[2]);if(!csv) throw std::runtime_error("Cannot write CSV");
    csv<<"tick,population,funds,revenue,expenses,surplus,funds_held,bus_riders,exports,visitors,educated,residents\n";
    uniques::Progress progress;
    nlohmann::json unlocks=nlohmann::json::array();
    uint32_t nextTick=0,lateEarned=0;
    for(const auto& segment:segments) {
      const uint32_t start=segment.at("tick").get<uint32_t>();
      if(start<nextTick) continue; // Native F9 continuation replay is not new activity.
      if(start!=nextTick) throw std::runtime_error("Journey has a tick gap");
      const auto path=segment.at("session").get<std::string>();
      int width=0,height=0;std::string error;
      if(!GameplaySessionSystem::readMapDimensions(path,width,height,&error)) throw std::runtime_error(error);
      CityMap map({width,height});RoadNetwork roads(map);EntityStore store;PopulationStore people;GameplaySessionState session;
      if(!GameplaySessionSystem::load(path,map,roads,store,people,session,&error)) throw std::runtime_error(error);
      const auto beforeLoad=progress;
      for(size_t index:{0u,4u,5u}) progress.peak(index,session.uniqueProgress.values[index]);
      for(const auto& item:uniques::catalog) if(!beforeLoad.unlocked(item.id) && progress.unlocked(item.id))
        unlocks.push_back({{"key",item.key},{"tick",session.tick},{"population",people.getTotalPopulation()},
          {"source","legacy checkpoint"}});
      session.uniqueProgress=progress;
      for(int tick=0;tick<segment.at("ticks").get<int>();++tick) {
        const auto before=progress;
        const auto state=GameplaySessionSystem::advance(map,roads,store,people,session);
        progress=session.uniqueProgress;
        for(const auto& item:uniques::catalog) if(!before.unlocked(item.id) && progress.unlocked(item.id))
          unlocks.push_back({{"key",item.key},{"tick",session.tick},{"population",people.getTotalPopulation()}});
        csv<<session.tick<<','<<people.getTotalPopulation()<<','<<session.funds<<','<<state.treasuryRevenue<<','
          <<state.treasuryExpenses<<','<<state.treasuryRevenue-state.treasuryExpenses;
        for(const auto value:progress.values) csv<<','<<value;
        csv<<'\n';
        if(people.getTotalPopulation()>=10000) {
          lateEarned=0;for(const auto& item:uniques::catalog) lateEarned+=progress.unlocked(item.id);
        }
      }
      nextTick=session.tick;
    }
    if(!csv || lateEarned<2) throw std::runtime_error("Journey must earn two uniques by 10,000 residents");
    std::cout<<nlohmann::json({{"ticks",nextTick},{"earned_at_10000",lateEarned},{"unlocks",unlocks},
      {"progress",progress.values}}).dump(2)<<'\n';
    return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
