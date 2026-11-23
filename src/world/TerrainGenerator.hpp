#pragma once

#include <cstdint>
#include <optional>

#include "src/world/CityMap.hpp"

struct TerrainBuildArea {
  Coord min;
  Coord max;  // Inclusive tile coordinates

  bool contains(Coord pos) const {
    return pos.x >= min.x && pos.x <= max.x &&
           pos.y >= min.y && pos.y <= max.y;
  }
};

// Fractions are share-of-map targets, rounded down and limited by available
// tiles. River/lake shorelines and hills follow a smoothed height field.
struct TerrainParams {
  float waterFraction = 0.18f;    // share of tiles marked water (type 2)
  float terrainFraction = 0.12f;  // share marked rough terrain/hills (type 1)
  int smoothingPasses = 4;        // box-blur passes; higher = larger, smoother regions
  std::optional<TerrainBuildArea> buildArea;  // Keep the base grade and buildable tile type.
};

struct TerrainStats {
  uint32_t waterTiles = 0;
  uint32_t terrainTiles = 0;
  uint32_t buildableTiles = 0;
};

// Stamps water (Tile::type == 2) and rough terrain (Tile::type == 1) onto a map
// with a continuous river and connected lake. Deterministic for seed + params.
// Writes tile types and persistent terrain/water-bed environmental data.
// Zones, buildings, roads, and land value are untouched.
// Intended to run on a freshly constructed (all-empty) map.
class TerrainGenerator {
public:
  static TerrainStats generate(CityMap& map, uint32_t seed, const TerrainParams& params = TerrainParams{});
};
