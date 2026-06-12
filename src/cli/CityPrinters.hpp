#pragma once

#include <cstdint>
#include <vector>

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/networks/Pathfinding.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/world/CityMap.hpp"

void printHelp();

const char* buildingTypeToString(BuildingType type);
const char* incomeBandToString(IncomeBand band);

// Reconstruct a population summary from loaded state (e.g. after --load-city).
PopulationSummary buildPopulationSummaryFromState(
  const EntityStore& store,
  const PopulationStore& population
);

void printMap(const CityMap& map);
void printTile(const CityMap& map, int x, int y);
void printZones(const CityMap& map);
void printDemand(uint32_t seed);
void printBuildings(const EntityStore& store);
void printConnectivityMap(const CityMap& map, const RoadNetwork& network);
void printPath(const Pathfinding::Path& path);
void printPopulationSummary(const PopulationSummary& summary);
void printPopulationGroups(const PopulationStore& population);
void printTrafficSummary(const TrafficSummary& summary);
void printTopCongestedEdges(const std::vector<EdgeTrafficData>& edges);
void printRouteDiagnosticsFilter(const RouteDiagnosticsFilter& filter);
void printBudgetSummary(const EconomyState& economy);
void printServiceSummary(const ServiceCoverageSummary& summary);
void printSnapshotInspection(const CitySnapshot& snapshot, const SnapshotLoadDiagnostics& diagnostics);
