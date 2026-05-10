#include <filesystem>

#include <gtest/gtest.h>

#include "src/persistence/SaveLoadSystem.hpp"

TEST(SaveLoadSystemTests, SaveAndLoadRoundTripPreservesCoreState) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  map.getTile({2, 2}).zone = 1;
  map.getTile({2, 2}).pollution = 0.15f;

  EntityId buildingId = store.createBuilding(BuildingType::Residential, {2, 2}, 40);
  Building* building = store.getBuilding(buildingId);
  ASSERT_NE(building, nullptr);
  building->occupancy = 30;
  map.getTile({2, 2}).buildingId = static_cast<uint32_t>(buildingId);

  population.createGroup(IncomeBand::Middle, 20, 15);

  roads.buildRoad({1, 1}, {2, 1});
  roads.buildRoad({2, 1}, {3, 1});
  roads.updateCongestion({1, 1}, {2, 1}, 5.0f);

  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_save_load_roundtrip.json";

  ASSERT_TRUE(SaveLoadSystem::saveToFile(filePath.string(), map, roads, store, population));

  CitySnapshot snapshot;
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot));
  ASSERT_EQ(snapshot.width, 8);
  ASSERT_EQ(snapshot.height, 8);

  CityMap loadedMap({snapshot.width, snapshot.height});
  RoadNetwork loadedRoads(loadedMap);
  EntityStore loadedStore;
  PopulationStore loadedPopulation;

  ASSERT_TRUE(SaveLoadSystem::applySnapshot(
    snapshot,
    loadedMap,
    loadedRoads,
    loadedStore,
    loadedPopulation
  ));

  EXPECT_EQ(loadedStore.getBuildingCount(), 1u);
  EXPECT_EQ(loadedPopulation.getGroupCount(), 1u);
  EXPECT_EQ(loadedRoads.getRoadCount(), 2u);

  const Tile& loadedTile = loadedMap.getTile({2, 2});
  EXPECT_EQ(loadedTile.zone, 1);
  EXPECT_FLOAT_EQ(loadedTile.pollution, 0.15f);
  EXPECT_EQ(loadedTile.buildingId, static_cast<uint32_t>(buildingId));

  const Building* loadedBuilding = loadedStore.getBuilding(buildingId);
  ASSERT_NE(loadedBuilding, nullptr);
  EXPECT_EQ(loadedBuilding->occupancy, 30);

  EXPECT_GT(loadedRoads.getCongestion({1, 1}, {2, 1}), 0.0f);

  std::filesystem::remove(filePath);
}

TEST(SaveLoadSystemTests, LoadSnapshotFromMissingFileFails) {
  CitySnapshot snapshot;
  const std::filesystem::path missingPath =
    std::filesystem::temp_directory_path() / "urban_sim_core_missing_save_file.json";

  std::filesystem::remove(missingPath);
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(missingPath.string(), snapshot));
}
