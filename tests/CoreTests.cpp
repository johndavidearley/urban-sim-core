#include "gtest/gtest.h"
#include "src/core/EntityId.hpp"
#include "src/world/CityMap.hpp"

TEST(EntityIdTests, GenerateUniqueIds) {
  auto id1 = EntityIdUtils::generateEntityId();
  auto id2 = EntityIdUtils::generateEntityId();
  
  EXPECT_NE(id1, id2);
  EXPECT_TRUE(EntityIdUtils::isValid(id1));
  EXPECT_TRUE(EntityIdUtils::isValid(id2));
}

TEST(CityMapTests, CreateAndAccess) {
  CityMap map({32, 32});
  
  EXPECT_EQ(map.getDimensions().x, 32);
  EXPECT_EQ(map.getDimensions().y, 32);
  EXPECT_EQ(map.getTileCount(), 32u * 32u);
}

TEST(CityMapTests, TileInitialization) {
  CityMap map({16, 16});
  
  auto& tile = map.getTile({5, 5});
  EXPECT_EQ(tile.position.x, 5);
  EXPECT_EQ(tile.position.y, 5);
  EXPECT_EQ(tile.zone, 0);
  EXPECT_EQ(tile.type, 0);
}

TEST(CityMapTests, BoundsChecking) {
  CityMap map({10, 10});
  
  EXPECT_TRUE(map.isValid({0, 0}));
  EXPECT_TRUE(map.isValid({9, 9}));
  EXPECT_FALSE(map.isValid({-1, 0}));
  EXPECT_FALSE(map.isValid({10, 10}));
  EXPECT_FALSE(map.isValid({5, -1}));
}

TEST(CityMapTests, InvalidCreation) {
  EXPECT_THROW(CityMap({0, 0}), std::invalid_argument);
  EXPECT_THROW(CityMap({-1, 10}), std::invalid_argument);
}
