#include "src/cli/CityPrinters.hpp"

#include <array>
#include <iomanip>
#include <iostream>

#include "src/world/Zoning.hpp"

void printHelp() {
  std::cout << "UrbanSimCore CLI v0.1.0\n"
            << "Usage: UrbanSimCore-cli [options]\n"
            << "Options:\n"
            << "  --size SIZE              Map size (default: 64)\n"
            << "  --ticks N                Number of ticks to simulate (default: 100)\n"
            << "  --seed SEED              Random seed (default: 42)\n"
            << "  --print-map              Print ASCII map representation and exit\n"
            << "  --print-tile X Y         Print detailed info for tile at (X,Y)\n"
            << "  --generate-terrain       Procedurally generate water/terrain from --seed\n"
            << "  --terrain-water FRAC     Target water coverage 0.0-1.0 (implies --generate-terrain)\n"
            << "  --zone-rect X1 Y1 X2 Y2 TYPE  Apply zoning to rectangle\n"
            << "  --print-zones            Print zoning map and exit\n"
            << "  --print-demand           Print zoning demand stub and exit\n"
            << "  --run-growth N           Run N growth steps and print summary\n"
            << "  --district-pressure-pool POOL  Apply district shared-budget pressure while running growth\n"
            << "  --print-growth-pressure  Print per-step district pressure multipliers during growth\n"
            << "  --export-growth-pressure FILE  Export per-step district pressure CSV for offline calibration\n"
            << "  --compare-growth-pressure FILE_A FILE_B  Compare two growth-pressure CSV reports\n"
            << "  --rank-growth-pressure BASE CANDIDATE  Rank candidate reports vs baseline (repeatable)\n"
            << "  --run-policy-sweep OUT_DIR  Run seed/cap/allocation sweep and auto-rank reports\n"
            << "  --sweep-district DIST_ID  District to mutate during sweep scenarios\n"
            << "  --sweep-seeds A,B,C  Comma-separated seed list for sweep scenarios\n"
            << "  --sweep-caps A,B,C  Comma-separated service cap list (-1 means uncapped)\n"
            << "  --sweep-allocations A,B,C  Comma-separated allocation list (0.0-1.0)\n"
            << "  --sweep-manifest-all-districts  Emit per-scenario per-district breakdown manifest\n"
            << "  --print-growth-summary   Print growth fill-rate summary\n"
            << "  --seed-population N      Allocate N residents to housing/jobs\n"
            << "  --print-population-summary  Print population/job summary\n"
            << "  --print-population-groups   Print grouped population composition\n"
            << "  --print-buildings        Print all spawned buildings\n"
            << "  --place-road X1 Y1 X2 Y2  Build a road segment between tiles\n"
            << "  --connectivity-map       Print connectivity status and exit\n"
            << "  --find-path X1 Y1 X2 Y2  Find shortest path from (X1,Y1) to (X2,Y2)\n"
            << "  --run-commute-simulation Run commute simulation for all employed\n"
            << "  --print-traffic-summary  Print traffic congestion and commute metrics\n"
            << "  --print-top-edges N      Print top N most congested edges\n"
            << "  --traffic-origin X Y     Filter route diagnostics by origin coordinate\n"
            << "  --traffic-destination X Y Filter route diagnostics by destination coordinate\n"
            << "  --run-economy-calculation Run economy/tax calculation\n"
            << "  --print-budget-summary    Print revenue/expense/economic health summary\n"
            << "  --add-service TYPE X Y DIST  Add service facility and max road distance\n"
            << "  --run-service-evaluation  Evaluate service coverage from facilities\n"
            << "  --print-service-summary   Print service coverage and satisfaction\n"
            << "  --print-city-summary      Print consolidated city metrics summary\n"
            << "  --create-district NAME X1 Y1 X2 Y2  Create a district (min to max corners)\n"
            << "  --list-districts         List all districts\n"
            << "  --print-district-summary DIST_ID  Print metrics for a district\n"
            << "  --print-district-balancing POOL  Print all district allocations under shared service budget pool\n"
            << "  --set-district-tax DIST_ID TYPE RATE  Set tax rate (residential|commercial|industrial|income)\n"
            << "  --set-district-service DIST_ID FIRE POLICE HEALTH EDUCATION  Set service priorities (0-1)\n"
            << "  --set-district-allocation DIST_ID PERCENT  Set district service allocation share (0-1)\n"
            << "  --set-district-budget-cap DIST_ID AMOUNT  Set district service budget cap (negative disables cap)\n"
            << "  --assign-facility DIST_ID FACILITY_ID  Assign service facility to district\n"
            << "  --unassign-facility DIST_ID FACILITY_ID  Remove service facility from district\n"
            << "  --print-district-facilities DIST_ID  Print facilities assigned to district\n"
            << "  --render-map FILE         Render top-down city snapshot to PPM file\n"
            << "  --render-scale N          Pixel size per tile when rendering (default: 8)\n"
            << "  --render-view X Y W H     Render viewport rectangle in tiles\n"
            << "  --save-city FILE          Save city snapshot JSON to FILE\n"
            << "  --load-city FILE          Load city snapshot JSON from FILE (prints migration diagnostics)\n"
            << "  --inspect-snapshot FILE   Inspect snapshot schema and structural summary\n"
            << "  --benchmark-phase5 N      Run N-tick Phase 5 performance benchmark\n"
            << "  --benchmark-phase5-focus PHASE  Time only one phase: ALL|GROWTH|POPULATION|TRAFFIC|ECONOMY|SERVICE\n"
            << "  --verify-replay N         Run deterministic replay check using N growth steps\n"
            << "  --simulate N              Run autonomous RCI-demand-driven city for N ticks\n"
            << "  --simulate-report FILE    Write per-tick simulation metrics to CSV\n"
            << "  --simulate-no-traffic     Skip the commute phase during --simulate\n"
            << "  --help                   Show this help message\n";
}

const char* buildingTypeToString(BuildingType type) {
  switch (type) {
    case BuildingType::Residential:
      return "Residential";
    case BuildingType::Commercial:
      return "Commercial";
    case BuildingType::Industrial:
      return "Industrial";
    default:
      return "Unknown";
  }
}

const char* incomeBandToString(IncomeBand band) {
  switch (band) {
    case IncomeBand::Low:
      return "Low";
    case IncomeBand::Middle:
      return "Middle";
    case IncomeBand::High:
      return "High";
    default:
      return "Unknown";
  }
}

PopulationSummary buildPopulationSummaryFromState(
  const EntityStore& store,
  const PopulationStore& population
) {
  PopulationSummary summary;
  summary.requestedPopulation = population.getTotalPopulation();
  summary.housedPopulation = summary.requestedPopulation;
  summary.employedPopulation = population.getTotalEmployed();
  summary.unemployedPopulation = summary.housedPopulation - summary.employedPopulation;

  uint32_t housingCapacity = 0;
  uint32_t jobCapacity = 0;
  uint32_t occupiedHousing = 0;
  uint32_t occupiedJobs = 0;

  for (const auto& [id, building] : store.getBuildings()) {
    (void)id;
    const uint32_t cap = static_cast<uint32_t>(std::max(0, building.capacity));
    const uint32_t occ = static_cast<uint32_t>(std::max(0, building.occupancy));
    if (building.type == BuildingType::Residential) {
      housingCapacity += cap;
      occupiedHousing += std::min(cap, occ);
    } else {
      jobCapacity += cap;
      occupiedJobs += std::min(cap, occ);
    }
  }

  summary.availableHousing = (housingCapacity > occupiedHousing) ? (housingCapacity - occupiedHousing) : 0;
  summary.availableJobs = (jobCapacity > occupiedJobs) ? (jobCapacity - occupiedJobs) : 0;
  summary.unemploymentRate = summary.housedPopulation > 0
    ? (static_cast<float>(summary.unemployedPopulation) / static_cast<float>(summary.housedPopulation))
    : 0.0f;

  for (const auto& [id, group] : population.getGroups()) {
    (void)id;
    switch (group.band) {
      case IncomeBand::Low:
        summary.lowIncomePopulation += group.size;
        summary.lowIncomeEmployed += group.employed;
        break;
      case IncomeBand::Middle:
        summary.middleIncomePopulation += group.size;
        summary.middleIncomeEmployed += group.employed;
        break;
      case IncomeBand::High:
        summary.highIncomePopulation += group.size;
        summary.highIncomeEmployed += group.employed;
        break;
    }
  }

  return summary;
}

void printMap(const CityMap& map) {
  glm::ivec2 dims = map.getDimensions();

  std::cout << "Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = empty, # = has road, ~ = water, T = terrain\n\n";

  // Print column headers
  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";

  // Print rows
  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      char symbol = '.';
      if (tile.hasRoad) symbol = '#';
      else if (tile.type == 1) symbol = 'T';
      else if (tile.type == 2) symbol = '~';
      std::cout << symbol;
    }
    std::cout << "\n";
  }
}

void printTile(const CityMap& map, int x, int y) {
  if (!map.isValid({x, y})) {
    std::cout << "Error: Tile (" << x << "," << y << ") is out of bounds\n";
    return;
  }

  const Tile& tile = map.getTile({x, y});

  std::cout << "Tile (" << x << "," << y << "):\n";
  std::cout << "  Position: (" << tile.position.x << "," << tile.position.y << ")\n";
  std::cout << "  Zone: " << Zoning::zoneToString(tile.zone) << " (" << tile.zone << ")\n";
  std::cout << "  Type: " << static_cast<int>(tile.type) << "\n";
  std::cout << "  Land Value: " << tile.landValue << "\n";
  std::cout << "  Pollution: " << tile.pollution << "\n";
  std::cout << "  Has Road: " << (tile.hasRoad ? "Yes" : "No") << "\n";
  std::cout << "  Connected to Road: " << (tile.connectedToRoad ? "Yes" : "No") << "\n";
  if (tile.buildingId != 0) {
    std::cout << "  Building ID: " << tile.buildingId << "\n";
  }
}

void printZones(const CityMap& map) {
  glm::ivec2 dims = map.getDimensions();

  std::cout << "Zone Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = none, R = residential, C = commercial, I = industrial, P = park\n\n";

  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";

  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      std::cout << Zoning::zoneToSymbol(tile.zone);
    }
    std::cout << "\n";
  }
}

void printDemand(uint32_t seed) {
  ZoneDemand demand = Zoning::calculateDemand(seed);
  std::cout << "Zone demand (stub):\n";
  std::cout << "  Residential: " << std::fixed << std::setprecision(3) << demand.residential << "\n";
  std::cout << "  Commercial:  " << std::fixed << std::setprecision(3) << demand.commercial << "\n";
  std::cout << "  Industrial:  " << std::fixed << std::setprecision(3) << demand.industrial << "\n";
}

void printBuildings(const EntityStore& store) {
  const auto& buildings = store.getBuildings();
  std::cout << "Buildings: " << buildings.size() << "\n";

  for (const auto& [id, building] : buildings) {
    std::cout << "  #" << id << " " << buildingTypeToString(building.type)
              << " at (" << building.position.x << "," << building.position.y << ")"
              << " cap=" << building.capacity << " occ=" << building.occupancy << "\n";
  }
}

void printConnectivityMap(const CityMap& map, const RoadNetwork& network) {
  glm::ivec2 dims = map.getDimensions();

  std::cout << "Connectivity Map (" << dims.x << "x" << dims.y << "):\n";
  std::cout << "  Legend: . = disconnected, X = connected, # = road only\n\n";

  // Print column headers
  std::cout << "    ";
  for (int x = 0; x < dims.x; x += 5) {
    std::cout << std::setw(5) << x;
  }
  std::cout << "\n";

  // Print rows
  for (int y = 0; y < dims.y; ++y) {
    std::cout << std::setw(3) << y << " ";
    for (int x = 0; x < dims.x; ++x) {
      char symbol = '.';
      if (network.isConnected({x, y})) {
        symbol = 'X';
      } else if (map.getTile({x, y}).hasRoad) {
        symbol = '#';
      }
      std::cout << symbol;
    }
    std::cout << "\n";
  }
}

void printPath(const Pathfinding::Path& path) {
  if (!path.found) {
    std::cout << "No path found between points.\n";
    return;
  }

  std::cout << "Path found (distance: " << std::fixed << std::setprecision(1)
            << path.totalDistance << "):\n";
  for (size_t i = 0; i < path.waypoints.size(); ++i) {
    if (i > 0) std::cout << " -> ";
    std::cout << "(" << path.waypoints[i].x << "," << path.waypoints[i].y << ")";
  }
  std::cout << "\n";
}

void printPopulationSummary(const PopulationSummary& summary) {
  std::cout << "Population Summary:\n";
  std::cout << "  Requested: " << summary.requestedPopulation << "\n";
  std::cout << "  Housed: " << summary.housedPopulation << "\n";
  std::cout << "  Employed: " << summary.employedPopulation << "\n";
  std::cout << "  Unemployed: " << summary.unemployedPopulation << "\n";
  std::cout << "  Available Housing: " << summary.availableHousing << "\n";
  std::cout << "  Available Jobs: " << summary.availableJobs << "\n";
  std::cout << "  Unemployment: " << std::fixed << std::setprecision(1)
            << (summary.unemploymentRate * 100.0f) << "%\n";
  std::cout << "  Composition (pop): Low=" << summary.lowIncomePopulation
            << ", Middle=" << summary.middleIncomePopulation
            << ", High=" << summary.highIncomePopulation << "\n";
  std::cout << "  Composition (employed): Low=" << summary.lowIncomeEmployed
            << ", Middle=" << summary.middleIncomeEmployed
            << ", High=" << summary.highIncomeEmployed << "\n";
}

void printPopulationGroups(const PopulationStore& population) {
  std::cout << "Population Groups: " << population.getGroupCount() << "\n";
  for (const auto& [id, group] : population.getGroups()) {
    std::cout << "  #" << id << " " << incomeBandToString(group.band)
              << " size=" << group.size
              << " employed=" << group.employed << "\n";
  }
}

void printTrafficSummary(const TrafficSummary& summary) {
  std::cout << "Traffic Summary:\n";
  std::cout << "  Commuting Population: " << summary.commutingPopulation << "\n";
  std::cout << "  Average Commute Time: " << std::fixed << std::setprecision(2)
            << summary.averageCommuteTime << "\n";
  std::cout << "  Total Commute Burden: " << summary.totalCommuteBurden << "\n";
  std::cout << "  Max Edge Congestion: " << std::fixed << std::setprecision(3)
            << (summary.maxEdgeCongestion * 100.0f) << "%\n";
  std::cout << "  Average Edge Congestion: " << std::fixed << std::setprecision(3)
            << (summary.averageEdgeCongestion * 100.0f) << "%\n";
  std::cout << "  Congested Edges: " << summary.congestionDetectedEdges << "\n";
}

void printTopCongestedEdges(const std::vector<EdgeTrafficData>& edges) {
  std::cout << "Top Congested Edges:\n";
  for (size_t i = 0; i < edges.size(); ++i) {
    const auto& edge = edges[i];
    std::cout << "  #" << (i + 1) << " (" << edge.from.x << "," << edge.from.y
              << ")->(" << edge.to.x << "," << edge.to.y << ")"
              << " congestion=" << std::fixed << std::setprecision(1)
              << (edge.congestion * 100.0f) << "%"
              << " commuters=" << edge.totalCommuters << "\n";
  }
}

void printRouteDiagnosticsFilter(const RouteDiagnosticsFilter& filter) {
  if (!filter.hasOrigin && !filter.hasDestination) {
    return;
  }

  std::cout << "Route Diagnostics Filter:";
  if (filter.hasOrigin) {
    std::cout << " origin=(" << filter.origin.x << "," << filter.origin.y << ")";
  }
  if (filter.hasDestination) {
    std::cout << " destination=(" << filter.destination.x << "," << filter.destination.y << ")";
  }
  std::cout << "\n";
}

void printBudgetSummary(const EconomyState& economy) {
  std::cout << "Budget Summary:\n";
  std::cout << "  Residential Tax Revenue: " << economy.residentialTaxRevenue << "\n";
  std::cout << "  Commercial Tax Revenue: " << economy.commercialTaxRevenue << "\n";
  std::cout << "  Industrial Tax Revenue: " << economy.industrialTaxRevenue << "\n";
  std::cout << "  Total Revenue: " << economy.totalRevenue << "\n";
  std::cout << "  Residential Maintenance: " << economy.residentialMaintenance << "\n";
  std::cout << "  Commercial Maintenance: " << economy.commercialMaintenance << "\n";
  std::cout << "  Industrial Maintenance: " << economy.industrialMaintenance << "\n";
  std::cout << "  Total Expenses: " << economy.totalExpenses << "\n";
  std::cout << "  Balance: " << economy.balance << "\n";
  std::cout << "  Average Land Value: " << std::fixed << std::setprecision(2)
            << economy.averageLandValue << "\n";
  std::cout << "  Economic Health: " << std::fixed << std::setprecision(1)
            << (economy.economicHealth * 100.0f) << "%\n";
}

void printServiceSummary(const ServiceCoverageSummary& summary) {
  std::cout << "Service Summary:\n";
  std::cout << "  Total Buildings: " << summary.totalBuildings << "\n";
  std::cout << "  Serviced Buildings: " << summary.servicedBuildings << "\n";
  std::cout << "  Fire Coverage: " << std::fixed << std::setprecision(1)
            << (summary.fireCoverage * 100.0f) << "%\n";
  std::cout << "  Police Coverage: " << std::fixed << std::setprecision(1)
            << (summary.policeCoverage * 100.0f) << "%\n";
  std::cout << "  Health Coverage: " << std::fixed << std::setprecision(1)
            << (summary.healthCoverage * 100.0f) << "%\n";
  std::cout << "  Education Coverage: " << std::fixed << std::setprecision(1)
            << (summary.educationCoverage * 100.0f) << "%\n";
  std::cout << "  Overall Coverage: " << std::fixed << std::setprecision(1)
            << (summary.overallCoverage * 100.0f) << "%\n";
  std::cout << "  Satisfaction: " << std::fixed << std::setprecision(1)
            << (summary.satisfaction * 100.0f) << "%\n";
}

void printSnapshotInspection(const CitySnapshot& snapshot, const SnapshotLoadDiagnostics& diagnostics) {
  size_t roadTileCount = 0;
  size_t zonedTileCount = 0;
  std::array<size_t, 5> zoneHistogram = {0, 0, 0, 0, 0};

  for (const SerializedTile& tile : snapshot.tiles) {
    if (tile.hasRoad) {
      ++roadTileCount;
    }
    if (tile.zone > 0 && tile.zone < static_cast<int>(zoneHistogram.size())) {
      ++zonedTileCount;
      ++zoneHistogram[static_cast<size_t>(tile.zone)];
    }
  }

  size_t residentialBuildings = 0;
  size_t commercialBuildings = 0;
  size_t industrialBuildings = 0;
  for (const Building& building : snapshot.buildings) {
    switch (building.type) {
      case BuildingType::Residential:
        ++residentialBuildings;
        break;
      case BuildingType::Commercial:
        ++commercialBuildings;
        break;
      case BuildingType::Industrial:
        ++industrialBuildings;
        break;
    }
  }

  std::cout << "Snapshot Inspection:\n";
  std::cout << "  Schema: sourceVersion=" << diagnostics.sourceVersion
            << ", targetVersion=" << diagnostics.targetVersion
            << ", migrated=" << (diagnostics.migrationApplied ? "yes" : "no")
            << ", path=" << diagnostics.migrationPath << "\n";
  std::cout << "  Map: " << snapshot.width << "x" << snapshot.height
            << " tiles=" << snapshot.tiles.size()
            << " roads=" << snapshot.roads.size()
            << " roadTiles=" << roadTileCount << "\n";
  std::cout << "  Zoning: zonedTiles=" << zonedTileCount
            << " (R=" << zoneHistogram[1]
            << " C=" << zoneHistogram[2]
            << " I=" << zoneHistogram[3]
            << " P=" << zoneHistogram[4] << ")\n";
  std::cout << "  Buildings: total=" << snapshot.buildings.size()
            << " (R=" << residentialBuildings
            << " C=" << commercialBuildings
            << " I=" << industrialBuildings << ")\n";
  std::cout << "  Population Groups: " << snapshot.populationGroups.size() << "\n";
}
