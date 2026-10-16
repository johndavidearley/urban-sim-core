#pragma once
#include "src/persistence/SaveLoadSystem.hpp"
#include "src/systems/RegionalConnectionSystem.hpp"
#include "src/gameplay/TerminalTool.hpp"

// A gameplay record is authoritative. Tile ownership is a derived index and
// deliberately absent from standalone core snapshots. The snapshot must first
// pass core validation (or come from captureSnapshot); records must be parsed.
inline bool validateRegionalReservations(const CitySnapshot& snapshot,
  const std::vector<regional::Connection>& connections,const std::vector<Coord>& civicSites,
  std::string* error) {
  auto fail=[&](const char* message) { if (error) *error=message; return false; };
  std::vector<uint32_t> ownership(static_cast<size_t>(snapshot.width)*snapshot.height,0);
  for (const auto& c:connections) {
    if (!c.constructed) continue;
    const auto* spec=regional::terminalSpec(c.kind);
    if (!c.id || !spec || c.width!=spec->width || c.height!=spec->height ||
        c.origin.x<0 || c.origin.y<0 || static_cast<int64_t>(c.origin.x)+c.width>snapshot.width ||
        static_cast<int64_t>(c.origin.y)+c.height>snapshot.height)
      return fail("invalid constructed terminal footprint");
    for (int y=c.origin.y;y<c.origin.y+c.height;++y)
      for (int x=c.origin.x;x<c.origin.x+c.width;++x) {
        auto& cell=ownership[static_cast<size_t>(y)*snapshot.width+x];
        if (cell) return fail("constructed terminal footprints overlap");
        cell=c.id;
      }
  }
  for (const auto& tile:snapshot.tiles) {
    if (ownership[static_cast<size_t>(tile.y)*snapshot.width+tile.x] &&
        (tile.type==2 || tile.zone || tile.hasRoad || tile.buildingId))
      return fail("constructed terminal overlaps occupied terrain");
  }
  // Validate entity/edge records too: tile metadata alone is not authoritative
  // for a road restored by applySnapshot or an independently stored building.
  for (const auto& road:snapshot.roads)
    if (ownership[static_cast<size_t>(road.from.y)*snapshot.width+road.from.x] ||
        ownership[static_cast<size_t>(road.to.y)*snapshot.width+road.to.x])
      return fail("constructed terminal overlaps a road edge");
  for (const auto& building:snapshot.buildings)
    if (ownership[static_cast<size_t>(building.position.y)*snapshot.width+building.position.x])
      return fail("constructed terminal overlaps a building record");
  for (auto p:civicSites)
    if (p.x>=0 && p.y>=0 && p.x<snapshot.width && p.y<snapshot.height &&
        ownership[static_cast<size_t>(p.y)*snapshot.width+p.x])
      return fail("constructed terminal overlaps civic facility");
  return true;
}

inline void rebuildRegionalReservations(CityMap& map,const std::vector<regional::Connection>& connections) {
  const auto size=map.getDimensions();
  for (int y=0;y<size.y;++y) for (int x=0;x<size.x;++x) map.getTile({x,y}).regionalConnectionId=0;
  for (const auto& c:connections) if (c.constructed)
    for (int y=c.origin.y;y<c.origin.y+c.height;++y)
      for (int x=c.origin.x;x<c.origin.x+c.width;++x) map.getTile({x,y}).regionalConnectionId=c.id;
}
