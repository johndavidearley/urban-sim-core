#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "src/persistence/SaveLoadSystem.hpp"

using nlohmann::json;

TEST(SaveLoadSystemTests, SaveAndLoadRoundTripPreservesCoreState) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  map.getTile({2, 2}).zone = 1;
  map.pollution({2, 2}) = 0.15f;

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
  SnapshotLoadDiagnostics diagnostics;
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot, &diagnostics));
  ASSERT_EQ(snapshot.width, 8);
  ASSERT_EQ(snapshot.height, 8);
  EXPECT_EQ(diagnostics.sourceVersion, 1);
  EXPECT_EQ(diagnostics.targetVersion, 1);
  EXPECT_FALSE(diagnostics.migrationApplied);
  EXPECT_TRUE(diagnostics.validationPassed);
  EXPECT_STREQ(diagnostics.migrationPath.c_str(), "none");

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
  EXPECT_FLOAT_EQ(loadedMap.pollution({2, 2}), 0.15f);
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

TEST(SaveLoadSystemTests, MigratesLegacyVersionZeroSnapshotToCurrent) {
  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_legacy_v0_snapshot.json";

  json root;
  root["version"] = 0;
  root["width"] = 4;
  root["height"] = 4;
  root["tiles"] = json::array();
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      root["tiles"].push_back(json{
        {"x", x},
        {"y", y},
        {"type", 0},
        {"zone", 0},
        {"landValue", 100.0f},
        {"pollution", 0.0f},
        {"hasRoad", false}
      });
    }
  }
  root["buildings"] = json::array();

  std::ofstream out(filePath);
  ASSERT_TRUE(out.is_open());
  out << root.dump(2);
  out.close();

  CitySnapshot snapshot;
  SnapshotLoadDiagnostics diagnostics;
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot, &diagnostics));
  EXPECT_EQ(snapshot.version, 1);
  EXPECT_EQ(snapshot.width, 4);
  EXPECT_EQ(snapshot.height, 4);
  EXPECT_EQ(snapshot.tiles.size(), 16u);
  EXPECT_EQ(diagnostics.sourceVersion, 0);
  EXPECT_EQ(diagnostics.targetVersion, 1);
  EXPECT_TRUE(diagnostics.migrationApplied);
  EXPECT_TRUE(diagnostics.validationPassed);
  EXPECT_STREQ(diagnostics.migrationPath.c_str(), "v0->v1");

  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  EXPECT_TRUE(SaveLoadSystem::applySnapshot(snapshot, map, roads, store, population));

  std::filesystem::remove(filePath);
}

TEST(SaveLoadSystemTests, RejectsUnsupportedFutureSnapshotVersion) {
  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_unsupported_version_snapshot.json";

  json root;
  root["version"] = 99;
  root["map"] = {{"width", 2}, {"height", 2}};
  root["tiles"] = json::array();
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      root["tiles"].push_back(json{
        {"x", x},
        {"y", y},
        {"type", 0},
        {"zone", 0},
        {"landValue", 100.0f},
        {"pollution", 0.0f},
        {"hasRoad", false},
        {"connectedToRoad", false},
        {"connectedToPower", true},
        {"connectedToWater", true},
        {"buildingId", 0}
      });
    }
  }
  root["buildings"] = json::array();
  root["populationGroups"] = json::array();
  root["roads"] = json::array();

  std::ofstream out(filePath);
  ASSERT_TRUE(out.is_open());
  out << root.dump(2);
  out.close();

  CitySnapshot snapshot;
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot));

  std::filesystem::remove(filePath);
}

TEST(SaveLoadSystemTests, SaveAndLoadRoundTripPreservesOfficeBuilding) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;

  map.getTile({3, 3}).zone = 5;  // ZoneType::Office

  EntityId officeId = store.createBuilding(BuildingType::Office, {3, 3}, 25);
  Building* office = store.getBuilding(officeId);
  ASSERT_NE(office, nullptr);
  office->occupancy = 18;
  map.getTile({3, 3}).buildingId = static_cast<uint32_t>(officeId);

  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_save_load_office.json";

  ASSERT_TRUE(SaveLoadSystem::saveToFile(filePath.string(), map, roads, store, population));

  CitySnapshot snapshot;
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot));

  CityMap loadedMap({snapshot.width, snapshot.height});
  RoadNetwork loadedRoads(loadedMap);
  EntityStore loadedStore;
  PopulationStore loadedPopulation;
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot, loadedMap, loadedRoads, loadedStore, loadedPopulation));

  const Building* loadedOffice = loadedStore.getBuilding(officeId);
  ASSERT_NE(loadedOffice, nullptr);
  EXPECT_EQ(loadedOffice->type, BuildingType::Office);
  EXPECT_EQ(loadedOffice->occupancy, 18);
  EXPECT_EQ(loadedMap.getTile({3, 3}).zone, 5);

  std::filesystem::remove(filePath);
}

TEST(SaveLoadSystemTests, RejectsSnapshotWithOutOfRangeBuildingType) {
  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_invalid_building_type_snapshot.json";

  json root;
  root["version"] = 1;
  root["map"] = {{"width", 3}, {"height", 3}};
  root["tiles"] = json::array();
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 3; ++x) {
      root["tiles"].push_back(json{
        {"x", x},
        {"y", y},
        {"type", 0},
        {"zone", 0},
        {"landValue", 100.0f},
        {"pollution", 0.0f},
        {"hasRoad", false},
        {"connectedToRoad", false},
        {"connectedToPower", true},
        {"connectedToWater", true},
        {"buildingId", 0u}
      });
    }
  }
  root["buildings"] = json::array({json{
    {"id", 1},
    {"type", 99},  // beyond BuildingType::Office - must be rejected
    {"position", json{{"x", 0}, {"y", 0}}},
    {"capacity", 10},
    {"occupancy", 0}
  }});
  root["populationGroups"] = json::array();
  root["roads"] = json::array();

  std::ofstream out(filePath);
  ASSERT_TRUE(out.is_open());
  out << root.dump(2);
  out.close();

  CitySnapshot snapshot;
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot));

  std::filesystem::remove(filePath);
}

TEST(SaveLoadSystemTests, RejectsSnapshotWithInvalidBuildingReference) {
  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_invalid_building_ref_snapshot.json";

  json root;
  root["version"] = 1;
  root["map"] = {{"width", 3}, {"height", 3}};
  root["tiles"] = json::array();
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 3; ++x) {
      const uint32_t buildingId = (x == 1 && y == 1) ? 4242u : 0u;
      root["tiles"].push_back(json{
        {"x", x},
        {"y", y},
        {"type", 0},
        {"zone", 1},
        {"landValue", 100.0f},
        {"pollution", 0.0f},
        {"hasRoad", false},
        {"connectedToRoad", false},
        {"connectedToPower", true},
        {"connectedToWater", true},
        {"buildingId", buildingId}
      });
    }
  }
  root["buildings"] = json::array();
  root["populationGroups"] = json::array();
  root["roads"] = json::array();

  std::ofstream out(filePath);
  ASSERT_TRUE(out.is_open());
  out << root.dump(2);
  out.close();

  CitySnapshot snapshot;
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot));

  std::filesystem::remove(filePath);
}
