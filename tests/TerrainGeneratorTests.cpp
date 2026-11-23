#include "gtest/gtest.h"

#include <vector>

#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"

namespace {
std::vector<int> typeGrid(const CityMap& map) {
  const glm::ivec2 dims = map.getDimensions();
  std::vector<int> grid;
  grid.reserve(static_cast<size_t>(dims.x) * static_cast<size_t>(dims.y));
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      grid.push_back(map.getTile({x, y}).type);
    }
  }
  return grid;
}
} // namespace

TEST(TerrainGeneratorTests, SameSeedProducesIdenticalTerrain) {
  CityMap mapA({48, 48});
  CityMap mapB({48, 48});

  const TerrainStats statsA = TerrainGenerator::generate(mapA, 1234);
  const TerrainStats statsB = TerrainGenerator::generate(mapB, 1234);

  EXPECT_EQ(typeGrid(mapA), typeGrid(mapB));
  EXPECT_EQ(statsA.waterTiles, statsB.waterTiles);
  EXPECT_EQ(statsA.terrainTiles, statsB.terrainTiles);
}

TEST(TerrainGeneratorTests, BuildAreaKeepsBaseGradeAndDryBuildableTiles) {
  CityMap map({96, 96});
  TerrainParams params;
  params.buildArea = TerrainBuildArea{{4, 34}, {70, 66}};
  const auto stats = TerrainGenerator::generate(map, 42, params);

  EXPECT_EQ(stats.waterTiles, 1658u);
  EXPECT_EQ(stats.terrainTiles, 1105u);
  for (int y = 0; y < 96; ++y) {
    const float baseGrade = 8.f * (1.f - float(y) / 95.f) + 1.f;
    for (int x = 0; x < 96; ++x) {
      const Coord pos{x, y};
      const auto& cell = map.environment(pos);
      const auto type = map.getTile(pos).type;
      if (params.buildArea->contains(pos)) {
        ASSERT_EQ(type, 0) << x << ", " << y;
        ASSERT_EQ(cell.water, 0.) << x << ", " << y;
        ASSERT_FLOAT_EQ(cell.terrainMeters, baseGrade) << x << ", " << y;
      } else if (type == 2) {
        ASSERT_GT(cell.water, 0.) << x << ", " << y;
        ASSERT_LT(cell.bedMeters, cell.terrainMeters) << x << ", " << y;
      } else if (type == 1) {
        ASSERT_GT(cell.terrainMeters, baseGrade + 11.f) << x << ", " << y;
      }
    }
  }
}

TEST(TerrainGeneratorTests, DifferentSeedsProduceDifferentTerrain) {
  CityMap mapA({48, 48});
  CityMap mapB({48, 48});

  TerrainGenerator::generate(mapA, 1);
  TerrainGenerator::generate(mapB, 2);

  EXPECT_NE(typeGrid(mapA), typeGrid(mapB));
}

TEST(TerrainGeneratorTests, CoverageMatchesRequestedFractions) {
  CityMap map({50, 50});  // 2500 tiles
  TerrainParams params;
  params.waterFraction = 0.20f;
  params.terrainFraction = 0.10f;

  const TerrainStats stats = TerrainGenerator::generate(map, 99, params);

  // 20% of 2500 = 500 water, 10% = 250 terrain (exact-count selection).
  EXPECT_EQ(stats.waterTiles, 500u);
  EXPECT_EQ(stats.terrainTiles, 250u);
  EXPECT_EQ(stats.buildableTiles, 2500u - 500u - 250u);

  // Counts agree with the actual stamped grid.
  uint32_t water = 0;
  uint32_t terrain = 0;
  for (int t : typeGrid(map)) {
    if (t == 2) ++water;
    else if (t == 1) ++terrain;
  }
  EXPECT_EQ(water, stats.waterTiles);
  EXPECT_EQ(terrain, stats.terrainTiles);
}

TEST(TerrainGeneratorTests, OverlappingFractionsDoNotExceedMap) {
  CityMap map({16, 16});  // 256 tiles
  TerrainParams params;
  params.waterFraction = 0.8f;
  params.terrainFraction = 0.8f;  // together exceed 100%

  const TerrainStats stats = TerrainGenerator::generate(map, 7, params);

  EXPECT_EQ(stats.waterTiles + stats.terrainTiles + stats.buildableTiles, 256u);
  EXPECT_LE(stats.waterTiles + stats.terrainTiles, 256u);
}

TEST(TerrainGeneratorTests, WaterIsNotZonedOrBuilt) {
  CityMap map({40, 40});
  TerrainParams params;
  params.waterFraction = 0.25f;
  TerrainGenerator::generate(map, 555, params);

  // Zone the whole map; water tiles must stay unzoned.
  int zonedCount = 0;
  Zoning::applyZoneRect(map, {0, 0}, {39, 39}, ZoneType::Residential, &zonedCount);

  uint32_t waterTiles = 0;
  for (int y = 0; y < 40; ++y) {
    for (int x = 0; x < 40; ++x) {
      const Tile& tile = map.getTile({x, y});
      if (tile.type == 2) {
        ++waterTiles;
        EXPECT_EQ(tile.zone, static_cast<int>(ZoneType::None)) << "water at (" << x << "," << y << ")";
      }
    }
  }
  EXPECT_GT(waterTiles, 0u);
  EXPECT_EQ(static_cast<uint32_t>(zonedCount), 1600u - waterTiles);

  // Roads everywhere so road access is never the limiting factor, then grow.
  RoadNetwork roads(map);
  for (int y = 0; y < 40; ++y) {
    for (int x = 0; x < 39; ++x) {
      roads.buildRoad({x, y}, {x + 1, y});
    }
  }

  EntityStore store;
  ZoneDemand demand;
  demand.residential = 1.0f;
  demand.commercial = 1.0f;
  demand.industrial = 1.0f;
  for (int step = 0; step < 8; ++step) {
    GrowthSystem::runStep(map, store, demand, static_cast<uint32_t>(step + 1), 1.0f);
  }

  // No building may sit on a water tile.
  for (int y = 0; y < 40; ++y) {
    for (int x = 0; x < 40; ++x) {
      const Tile& tile = map.getTile({x, y});
      if (tile.type == 2) {
        EXPECT_EQ(tile.buildingId, 0u) << "building on water at (" << x << "," << y << ")";
      }
    }
  }
}

TEST(TerrainGeneratorTests, RiverFeedsOneConnectedLakeWithUsableShorelines) {
  for (int size : {32, 64, 96}) {
    std::vector<std::pair<uint32_t, TerrainParams>> cases{{1u, {}}, {42u, {}}, {555u, {}}};
    if (size == 96) {
      TerrainParams journey;
      journey.buildArea = TerrainBuildArea{{4, 34}, {70, 66}};
      cases.emplace_back(42u, journey);
    }
    for (const auto& [seed, params] : cases) {
      CityMap map({size, size});
      const auto stats = TerrainGenerator::generate(map, seed, params);
      std::vector<Coord> pending;
      std::vector<bool> seen(static_cast<size_t>(size * size), false);
      bool top = false, bottom = false;
      uint32_t shores = 0;
      for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
          if (map.getTile({x, y}).type != 2)
            continue;
          if (pending.empty()) {
            pending.push_back({x, y});
            seen[static_cast<size_t>(y * size + x)] = true;
          }
          top |= y == 0;
          bottom |= y == size - 1;
        }
      for (size_t i = 0; i < pending.size(); ++i) {
        const auto p = pending[i];
        for (Coord d : {Coord{-1, 0}, Coord{1, 0}, Coord{0, -1}, Coord{0, 1}}) {
          Coord n{p.x + d.x, p.y + d.y};
          if (!map.isValid(n))
            continue;
          if (map.getTile(n).type != 2) {
            ++shores;
            continue;
          }
          const auto index = static_cast<size_t>(n.y * size + n.x);
          if (!seen[index]) {
            seen[index] = true;
            pending.push_back(n);
          }
        }
      }
      EXPECT_EQ(pending.size(), stats.waterTiles) << size << " seed " << seed;
      EXPECT_TRUE(top);
      EXPECT_TRUE(bottom);
      EXPECT_GT(shores, static_cast<uint32_t>(size));
    }
  }
}
