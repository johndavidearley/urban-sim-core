#pragma once

#include "src/visualization/VisualizerTypes.hpp"

namespace visualizer {

bool hasRoadAdjacency(const RoadNetwork& roads, Coord coord);
bool resolveRoadAnchor(const RoadNetwork& roads, Coord coord, Coord& outAnchor);
int shortestRoadDistance(const RoadNetwork& roads, Coord start, Coord goal);
float serviceCoverageAtTile(const RoadNetwork& roads, Coord coord, const std::vector<ServiceFacility>& facilities);
float localCongestionAtTile(const RoadNetwork& roads, Coord coord);
float demandAtTile(const CityMap& map, const RoadNetwork& roads, Coord coord);
float happinessAtTile(
  const CityMap& map,
  const RoadNetwork& roads,
  Coord coord,
  const std::vector<ServiceFacility>& facilities,
  float wasteHappinessPenalty
);
RGB tileColor(
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  Coord coord,
  OverlayMode overlayMode,
  const std::vector<ServiceFacility>& facilities,
  const std::unordered_map<Coord, float, Vec2Hash>& routeHeatByTile,
  float wasteHappinessPenalty
);
RGB overlaySampleColor(OverlayMode mode, float value);
std::unordered_map<Coord, float, Vec2Hash> buildRouteHeatByTile(const RoadNetwork& roads);
std::unordered_map<Coord, float, Vec2Hash> buildRouteHeatByTileFromEdges(
  const std::vector<EdgeTrafficData>& edges
);
bool coordLess(const Coord& a, const Coord& b);
std::vector<Coord> collectBuildingCoords(const EntityStore& store, bool residentialOnly);
void cycleOriginFilter(LiveSimulationState& liveState, const EntityStore& store);
void cycleDestinationFilter(LiveSimulationState& liveState, const EntityStore& store);
void clearRouteFilters(LiveSimulationState& liveState);
void refreshRouteHeat(
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  LiveSimulationState& liveState,
  uint32_t seed
);
std::string routeFilterLabel(const RouteDiagnosticsFilter& filter);
float routeHeatAtTile(const std::unordered_map<Coord, float, Vec2Hash>& routeHeatByTile, Coord coord);

}  // namespace visualizer
