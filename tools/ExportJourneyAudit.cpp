#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include "src/world/CityMap.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

// A segment begins after native construction/policy input and before its ticks.
// Reload every segment so later gates and department rules cannot change the
// original journey's construction schedule. This audits pricing on current-rule
// continuations of the historical cities, not a new Phase 14 playthrough.
int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("Usage: export-journey-audit segments.json ticks.csv");
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("Cannot open segment manifest");
    const auto segments = nlohmann::json::parse(input);
    if (!segments.is_array() || segments.empty()) throw std::runtime_error("Empty segment manifest");
    std::ofstream csv(argv[2]);
    if (!csv) throw std::runtime_error("Cannot open tick report");
    csv << "segment,tick,population_before,population_after,exports,unit_price,export_revenue,flat_revenue\n";
    uint32_t earlyTicks = 0, lateTicks = 0, maxEarlyPopulation = 0;
    int64_t maxEarlyExports = 0, maxLateRevenue = 0;
    for (size_t index = 0; index < segments.size(); ++index) {
      const auto& segment = segments[index];
      const auto path = segment.at("session").get<std::string>();
      const int ticks = segment.at("ticks").get<int>();
      if (ticks <= 0 || ticks > 10000) throw std::runtime_error("Invalid segment tick count");
      int width = 0, height = 0;
      std::string error;
      if (!GameplaySessionSystem::readMapDimensions(path, width, height, &error))
        throw std::runtime_error(path + ": " + error);
      CityMap map({width, height});
      RoadNetwork roads(map);
      EntityStore store;
      PopulationStore population;
      GameplaySessionState session;
      if (!GameplaySessionSystem::load(path, map, roads, store, population, session, &error))
        throw std::runtime_error(path + ": " + error);
      if (session.autonomousGrowth) throw std::runtime_error("Journey has autonomous construction enabled");
      for (int tick = 0; tick < ticks; ++tick) {
        const auto before = population.getTotalPopulation();
        const auto state = GameplaySessionSystem::advance(map, roads, store, population, session);
        const auto after = population.getTotalPopulation();
        auto flat = state.economy;
        flat.exportFullPriceVolume = 0;
        EconomySystem::priceExports(flat, state.economy.exportedGoods);
        const auto actualRevenue = session.treasuryLedger.breakdown.revenue[5];
        const auto flatRevenue = TreasurySystem::breakdown(flat, 0.01, 0).revenue[5];
        csv << index << ',' << session.tick << ',' << before << ',' << after << ','
            << state.economy.exportedGoods << ',' << state.economy.exportUnitPrice / 100.0
            << ',' << actualRevenue << ',' << flatRevenue << '\n';
        // Include the crossing tick too: pricing must preserve a city that
        // starts below 2,000 even if migration takes it over during this tick.
        if (before < 2000 || after < 2000) {
          ++earlyTicks;
          maxEarlyPopulation = std::max(maxEarlyPopulation, std::min(before, after));
          maxEarlyExports = std::max(maxEarlyExports, state.economy.exportedGoods);
          if (state.economy.exportedGoods > 2000 || actualRevenue != flatRevenue)
            throw std::runtime_error("Early-city pricing changed at tick " + std::to_string(session.tick));
        }
        if (before >= 10000 && after >= 10000) {
          ++lateTicks;
          maxLateRevenue = std::max(maxLateRevenue, actualRevenue);
          if (actualRevenue > 907) throw std::runtime_error("Late-city export income exceeds $907");
        }
      }
    }
    if (!csv) throw std::runtime_error("Cannot write tick report");
    if (earlyTicks == 0 || maxEarlyPopulation < 1900 || lateTicks == 0)
      throw std::runtime_error("Journey lacks coverage near 2,000 or above 10,000 residents");
    std::cout << nlohmann::json({{"segments", segments.size()}, {"early_ticks", earlyTicks},
      {"max_early_population", maxEarlyPopulation}, {"max_early_exports", maxEarlyExports},
      {"late_ticks", lateTicks}, {"max_late_export_revenue", maxLateRevenue}}).dump(2) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
