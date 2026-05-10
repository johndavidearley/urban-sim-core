#include <gtest/gtest.h>
#include "src/world/CityMap.hpp"
#include "src/world/Tile.hpp"
#include "src/world/Zoning.hpp"

// Tests for Tile structure
TEST(TileTests, TileCreation) {
  Tile tile;
  tile.position = {5, 10};
  tile.zone = 1;
  tile.type = 0;
  
  EXPECT_EQ(tile.position.x, 5);
  EXPECT_EQ(tile.position.y, 10);
  EXPECT_EQ(tile.zone, 1);
  EXPECT_EQ(tile.type, 0);
}

TEST(TileTests, TileDefaults) {
  Tile tile;
  
  EXPECT_EQ(tile.landValue, 100.0f);
  EXPECT_EQ(tile.pollution, 0.0f);
  EXPECT_FALSE(tile.hasRoad);
  EXPECT_FALSE(tile.connectedToRoad);
  EXPECT_TRUE(tile.connectedToPower);   // Stub for MVP
  EXPECT_TRUE(tile.connectedToWater);   // Stub for MVP
  EXPECT_EQ(tile.buildingId, 0);
}

TEST(TileTests, TileZoneTypes) {
  Tile tile1, tile2, tile3;
  
  tile1.zone = 0;  // None
  tile2.zone = 1;  // Residential
  tile3.zone = 2;  // Commercial
  
  EXPECT_EQ(tile1.zone, 0);
  EXPECT_EQ(tile2.zone, 1);
  EXPECT_EQ(tile3.zone, 2);
}

TEST(TileTests, TileTerrain) {
  Tile terrain, water;
  
  terrain.type = 1;
  water.type = 2;
  
  EXPECT_EQ(terrain.type, 1);
  EXPECT_EQ(water.type, 2);
}

// Tests for CityMap
TEST(CityMapTests, MapCreation) {
  CityMap map({32, 32});
  
  EXPECT_EQ(map.getDimensions().x, 32);
  EXPECT_EQ(map.getDimensions().y, 32);
  EXPECT_EQ(map.getTileCount(), 32 * 32);
}

TEST(CityMapTests, MapSquare) {
  CityMap map({64, 64});
  
  EXPECT_EQ(map.getDimensions().x, 64);
  EXPECT_EQ(map.getDimensions().y, 64);
  EXPECT_EQ(map.getTileCount(), 64 * 64);
}

TEST(CityMapTests, MapRectangular) {
  CityMap map({100, 50});
  
  EXPECT_EQ(map.getDimensions().x, 100);
  EXPECT_EQ(map.getDimensions().y, 50);
  EXPECT_EQ(map.getTileCount(), 100 * 50);
}

TEST(CityMapTests, TilePositionInitialization) {
  CityMap map({16, 16});
  
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      const Tile& tile = map.getTile({x, y});
      EXPECT_EQ(tile.position.x, x);
      EXPECT_EQ(tile.position.y, y);
    }
  }
}

TEST(CityMapTests, TileZoneInitialization) {
  CityMap map({8, 8});
  
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      const Tile& tile = map.getTile({x, y});
      EXPECT_EQ(tile.zone, 0);  // All empty zones initially
      EXPECT_EQ(tile.type, 0);  // All empty terrain initially
    }
  }
}

TEST(CityMapTests, AccessTile) {
  CityMap map({16, 16});
  
  Tile& tile = map.getTile({7, 11});
  tile.zone = 1;
  tile.landValue = 150.0f;
  
  const Tile& sameTile = map.getTile({7, 11});
  EXPECT_EQ(sameTile.zone, 1);
  EXPECT_EQ(sameTile.landValue, 150.0f);
}

TEST(CityMapTests, BoundsCheckingCorners) {
  CityMap map({10, 10});
  
  EXPECT_TRUE(map.isValid({0, 0}));
  EXPECT_TRUE(map.isValid({9, 0}));
  EXPECT_TRUE(map.isValid({0, 9}));
  EXPECT_TRUE(map.isValid({9, 9}));
}

TEST(CityMapTests, BoundsCheckingEdges) {
  CityMap map({10, 10});
  
  EXPECT_FALSE(map.isValid({-1, 0}));
  EXPECT_FALSE(map.isValid({10, 0}));
  EXPECT_FALSE(map.isValid({0, -1}));
  EXPECT_FALSE(map.isValid({0, 10}));
}

TEST(CityMapTests, BoundsCheckingCenter) {
  CityMap map({10, 10});
  
  EXPECT_TRUE(map.isValid({4, 5}));
  EXPECT_TRUE(map.isValid({5, 4}));
  EXPECT_TRUE(map.isValid({5, 5}));
}

TEST(CityMapTests, BoundsCheckingLarge) {
  CityMap map({256, 256});
  
  EXPECT_TRUE(map.isValid({0, 0}));
  EXPECT_TRUE(map.isValid({255, 255}));
  EXPECT_FALSE(map.isValid({256, 256}));
  EXPECT_FALSE(map.isValid({-1, -1}));
}

TEST(CityMapTests, BoundsCheckingSmall) {
  CityMap map({1, 1});
  
  EXPECT_TRUE(map.isValid({0, 0}));
  EXPECT_FALSE(map.isValid({1, 0}));
  EXPECT_FALSE(map.isValid({0, 1}));
  EXPECT_FALSE(map.isValid({1, 1}));
}

TEST(CityMapTests, InvalidCreationZero) {
  EXPECT_THROW(CityMap({0, 0}), std::invalid_argument);
  EXPECT_THROW(CityMap({0, 10}), std::invalid_argument);
  EXPECT_THROW(CityMap({10, 0}), std::invalid_argument);
}

TEST(CityMapTests, InvalidCreationNegative) {
  EXPECT_THROW(CityMap({-1, 10}), std::invalid_argument);
  EXPECT_THROW(CityMap({10, -1}), std::invalid_argument);
  EXPECT_THROW(CityMap({-5, -5}), std::invalid_argument);
}

TEST(CityMapTests, TileModification) {
  CityMap map({8, 8});
  
  Tile& tile = map.getTile({3, 4});
  
  // Modify properties
  tile.zone = 2;
  tile.type = 1;
  tile.hasRoad = true;
  tile.connectedToRoad = true;
  tile.landValue = 250.0f;
  tile.pollution = 0.5f;
  tile.buildingId = 42;
  
  // Verify modifications persisted
  const Tile& sameTile = map.getTile({3, 4});
  EXPECT_EQ(sameTile.zone, 2);
  EXPECT_EQ(sameTile.type, 1);
  EXPECT_TRUE(sameTile.hasRoad);
  EXPECT_TRUE(sameTile.connectedToRoad);
  EXPECT_EQ(sameTile.landValue, 250.0f);
  EXPECT_EQ(sameTile.pollution, 0.5f);
  EXPECT_EQ(sameTile.buildingId, 42);
}

TEST(CityMapTests, MultipleTilesIndependent) {
  CityMap map({5, 5});
  
  // Modify multiple tiles
  map.getTile({0, 0}).zone = 1;
  map.getTile({4, 4}).zone = 2;
  map.getTile({2, 2}).zone = 3;
  
  // Verify independence
  EXPECT_EQ(map.getTile({0, 0}).zone, 1);
  EXPECT_EQ(map.getTile({4, 4}).zone, 2);
  EXPECT_EQ(map.getTile({2, 2}).zone, 3);
  EXPECT_EQ(map.getTile({1, 1}).zone, 0);  // Untouched
}

TEST(CityMapTests, LargeMapPerformance) {
  // Test that large map creation is fast
  CityMap map({256, 256});
  
  EXPECT_EQ(map.getTileCount(), 256 * 256);
  
  // Access corners should work
  EXPECT_NO_THROW(map.getTile({0, 0}));
  EXPECT_NO_THROW(map.getTile({255, 255}));
}

TEST(CityMapTests, OutOfBoundsAccess) {
  CityMap map({10, 10});
  
  EXPECT_THROW(map.getTile({-1, 5}), std::out_of_range);
  EXPECT_THROW(map.getTile({5, -1}), std::out_of_range);
  EXPECT_THROW(map.getTile({10, 5}), std::out_of_range);
  EXPECT_THROW(map.getTile({5, 10}), std::out_of_range);
}

// Tests for Zoning
TEST(ZoningTests, ParseZoneType) {
  ZoneType zone = ZoneType::None;

  EXPECT_TRUE(Zoning::parseZoneType("residential", zone));
  EXPECT_EQ(zone, ZoneType::Residential);

  EXPECT_TRUE(Zoning::parseZoneType("COM", zone));
  EXPECT_EQ(zone, ZoneType::Commercial);

  EXPECT_TRUE(Zoning::parseZoneType("INDUSTRIAL", zone));
  EXPECT_EQ(zone, ZoneType::Industrial);

  EXPECT_TRUE(Zoning::parseZoneType("park", zone));
  EXPECT_EQ(zone, ZoneType::Park);

  EXPECT_FALSE(Zoning::parseZoneType("invalid-zone", zone));
}

TEST(ZoningTests, ApplyZoneRectInclusive) {
  CityMap map({8, 8});
  int zonedCount = 0;

  EXPECT_TRUE(Zoning::applyZoneRect(
    map, {2, 2}, {4, 3}, ZoneType::Residential, &zonedCount
  ));
  EXPECT_EQ(zonedCount, 6);

  for (int y = 2; y <= 3; ++y) {
    for (int x = 2; x <= 4; ++x) {
      EXPECT_EQ(map.getTile({x, y}).zone, static_cast<int>(ZoneType::Residential));
      EXPECT_EQ(map.getTile({x, y}).landValue, 120.0f);
    }
  }
}

TEST(ZoningTests, ApplyZoneRectHandlesInvertedCorners) {
  CityMap map({8, 8});
  int zonedCount = 0;

  EXPECT_TRUE(Zoning::applyZoneRect(
    map, {5, 5}, {3, 4}, ZoneType::Commercial, &zonedCount
  ));
  EXPECT_EQ(zonedCount, 6);

  for (int y = 4; y <= 5; ++y) {
    for (int x = 3; x <= 5; ++x) {
      EXPECT_EQ(map.getTile({x, y}).zone, static_cast<int>(ZoneType::Commercial));
      EXPECT_EQ(map.getTile({x, y}).landValue, 140.0f);
    }
  }
}

TEST(ZoningTests, ApplyZoneRectOutOfBoundsFails) {
  CityMap map({8, 8});
  map.getTile({0, 0}).zone = static_cast<int>(ZoneType::Industrial);

  EXPECT_FALSE(Zoning::applyZoneRect(map, {-1, 0}, {2, 2}, ZoneType::Park));
  EXPECT_EQ(map.getTile({0, 0}).zone, static_cast<int>(ZoneType::Industrial));
}

TEST(ZoningTests, DemandIsDeterministicPerSeed) {
  const ZoneDemand first = Zoning::calculateDemand(42);
  const ZoneDemand second = Zoning::calculateDemand(42);
  const ZoneDemand third = Zoning::calculateDemand(43);

  EXPECT_FLOAT_EQ(first.residential, second.residential);
  EXPECT_FLOAT_EQ(first.commercial, second.commercial);
  EXPECT_FLOAT_EQ(first.industrial, second.industrial);

  EXPECT_NE(first.residential, third.residential);

  EXPECT_GE(first.residential, 0.0f);
  EXPECT_LE(first.residential, 1.0f);
  EXPECT_GE(first.commercial, 0.0f);
  EXPECT_LE(first.commercial, 1.0f);
  EXPECT_GE(first.industrial, 0.0f);
  EXPECT_LE(first.industrial, 1.0f);
}
