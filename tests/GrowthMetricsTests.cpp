#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/metrics/GrowthMetrics.hpp"
#include "src/world/Zoning.hpp"

TEST(GrowthMetricsTests, EmptyMapHasZeroMetrics) {
  CityMap map({8, 8});
  EntityStore store;

  const GrowthMetrics metrics = GrowthMetrics::collect(map, store);

  EXPECT_EQ(metrics.totalBuildings, 0);
  EXPECT_EQ(metrics.zonedResidential, 0);
  EXPECT_EQ(metrics.zonedCommercial, 0);
  EXPECT_EQ(metrics.zonedIndustrial, 0);
  EXPECT_FLOAT_EQ(metrics.residentialFillRate, 0.0f);
  EXPECT_FLOAT_EQ(metrics.commercialFillRate, 0.0f);
  EXPECT_FLOAT_EQ(metrics.industrialFillRate, 0.0f);
}

TEST(GrowthMetricsTests, ZonedTilesWithoutBuildingsHaveZeroFillRate) {
  CityMap map({8, 8});
  EntityStore store;

  EXPECT_TRUE(Zoning::applyZoneRect(map, {1, 1}, {3, 1}, ZoneType::Residential));
  EXPECT_TRUE(Zoning::applyZoneRect(map, {1, 2}, {2, 2}, ZoneType::Commercial));
  EXPECT_TRUE(Zoning::applyZoneRect(map, {1, 3}, {1, 4}, ZoneType::Industrial));

  const GrowthMetrics metrics = GrowthMetrics::collect(map, store);

  EXPECT_EQ(metrics.zonedResidential, 3);
  EXPECT_EQ(metrics.zonedCommercial, 2);
  EXPECT_EQ(metrics.zonedIndustrial, 2);
  EXPECT_EQ(metrics.builtResidential, 0);
  EXPECT_EQ(metrics.builtCommercial, 0);
  EXPECT_EQ(metrics.builtIndustrial, 0);
}

TEST(GrowthMetricsTests, MixedZoningAndBuildingsAreSummarized) {
  CityMap map({10, 10});
  EntityStore store;

  EXPECT_TRUE(Zoning::applyZoneRect(map, {1, 1}, {4, 1}, ZoneType::Residential));
  EXPECT_TRUE(Zoning::applyZoneRect(map, {1, 2}, {3, 2}, ZoneType::Commercial));
  EXPECT_TRUE(Zoning::applyZoneRect(map, {1, 3}, {2, 3}, ZoneType::Industrial));

  const EntityId r = store.createBuilding(BuildingType::Residential, {1, 1}, 8);
  const EntityId c = store.createBuilding(BuildingType::Commercial, {1, 2}, 20);
  const EntityId i = store.createBuilding(BuildingType::Industrial, {1, 3}, 24);

  map.getTile({1, 1}).buildingId = r;
  map.getTile({1, 2}).buildingId = c;
  map.getTile({1, 3}).buildingId = i;

  const GrowthMetrics metrics = GrowthMetrics::collect(map, store);

  EXPECT_EQ(metrics.totalBuildings, 3);
  EXPECT_EQ(metrics.zonedResidential, 4);
  EXPECT_EQ(metrics.zonedCommercial, 3);
  EXPECT_EQ(metrics.zonedIndustrial, 2);

  EXPECT_EQ(metrics.builtResidential, 1);
  EXPECT_EQ(metrics.builtCommercial, 1);
  EXPECT_EQ(metrics.builtIndustrial, 1);

  EXPECT_NEAR(metrics.residentialFillRate, 0.25f, 0.001f);
  EXPECT_NEAR(metrics.commercialFillRate, 1.0f / 3.0f, 0.001f);
  EXPECT_NEAR(metrics.industrialFillRate, 0.5f, 0.001f);
}
