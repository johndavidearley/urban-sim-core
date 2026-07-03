#pragma once

#include <cstdint>

#include "src/world/CityMap.hpp"

// Tuning for procedural terrain. Fractions are share-of-map targets; the
// generator hits them exactly (modulo integer rounding) by selecting the
// lowest/highest tiles of a smoothed height field.
struct TerrainParams {
  float waterFraction = 0.18f;    // share of tiles marked water (type 2)
  float terrainFraction = 0.12f;  // share marked rough terrain/hills (type 1)
  int smoothingPasses = 4;        // box-blur passes; higher = larger, smoother regions
};

struct TerrainStats {
  uint32_t waterTiles = 0;
  uint32_t terrainTiles = 0;
  uint32_t buildableTiles = 0;
};

// Stamps water (Tile::type == 2) and rough terrain (Tile::type == 1) onto a map
// from a seeded, smoothed height field. Deterministic for a given seed + params.
// Only writes Tile::type; zones, buildings, roads, and land value are untouched.
// Intended to run on a freshly constructed (all-empty) map.
class TerrainGenerator {
public:
  static TerrainStats generate(CityMap& map, uint32_t seed, const TerrainParams& params = TerrainParams{});
};
