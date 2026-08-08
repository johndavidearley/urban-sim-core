#include "gtest/gtest.h"

#include "src/visualization/IsometricProjection.hpp"

TEST(IsometricProjectionTests, ProjectsAlongDiamondAxes) {
  const IsometricProjection projection(40, 20, {300, 50});

  const ScreenPoint east = projection.tileTop({1, 0});
  const ScreenPoint south = projection.tileTop({0, 1});

  EXPECT_EQ(east.x, 320);
  EXPECT_EQ(east.y, 60);
  EXPECT_EQ(south.x, 280);
  EXPECT_EQ(south.y, 60);
}

TEST(IsometricProjectionTests, TileCentersRoundTripAcrossMap) {
  const IsometricProjection projection(36, 18, {640, -120});

  for (int y = 0; y < 64; y += 7) {
    for (int x = 0; x < 64; x += 5) {
      const Coord tile{x, y};
      EXPECT_EQ(projection.screenToTile(projection.tileCenter(tile)), tile);
    }
  }
}

TEST(IsometricProjectionTests, InvalidSizesAreClamped) {
  const IsometricProjection projection(0, -4);
  EXPECT_EQ(projection.tileWidth(), 2);
  EXPECT_EQ(projection.tileHeight(), 2);
}
