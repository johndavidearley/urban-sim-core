#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

#include "src/persistence/SaveLoadSystem.hpp"

using nlohmann::json;

TEST(SaveLoadSystemTests, RestoresAllocatorIncludingRemovedEntitiesAndLegacyFallback) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  const auto live = store.createBuilding(BuildingType::Residential,{2,2},40);
  const auto removed = store.createBuilding(BuildingType::Commercial,{3,2},20);
  store.removeBuilding(removed);
  auto snapshot = SaveLoadSystem::captureSnapshot(map,roads,store,people);
  const auto expected = snapshot.nextEntityId;
  people.createGroup(10,0);
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot,map,roads,store,people));
  EXPECT_EQ(people.createGroup(10,0),expected);
  // Invalid counters must reject before replacing the destination world.
  snapshot.nextEntityId = live;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot,map,roads,store,people));
  EXPECT_EQ(people.getGroupCount(),1u);
  snapshot.nextEntityId = 0;
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot,map,roads,store,people));
  EXPECT_EQ(people.createGroup(10,0),static_cast<uint64_t>(live)+1);
}

TEST(SaveLoadSystemTests, SnapshotOrderDoesNotDependOnStoreHistory) {
  CityMap map({16,16}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  std::vector<EntityId> removed;
  for (int i = 0; i < 64; ++i) {
    const auto id = store.createBuilding(BuildingType::Residential,{i%16,i/16},8);
    people.createGroup(4,0);
    if (i % 3 != 0) removed.push_back(id);
  }
  for (const auto id : removed) store.removeBuilding(id);
  const auto live = SaveLoadSystem::captureSnapshot(map,roads,store,people);
  CityMap loadedMap({16,16}); RoadNetwork loadedRoads(loadedMap);
  EntityStore loadedStore; PopulationStore loadedPeople;
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(live,loadedMap,loadedRoads,loadedStore,loadedPeople));
  const auto reloaded = SaveLoadSystem::captureSnapshot(loadedMap,loadedRoads,loadedStore,loadedPeople);
  ASSERT_EQ(live.buildings.size(),reloaded.buildings.size());
  ASSERT_EQ(live.populationGroups.size(),reloaded.populationGroups.size());
  for (size_t i = 0; i < live.buildings.size(); ++i) {
    EXPECT_EQ(live.buildings[i].id,reloaded.buildings[i].id);
    if (i > 0) EXPECT_LT(live.buildings[i-1].id,live.buildings[i].id);
  }
  for (size_t i = 0; i < live.populationGroups.size(); ++i) {
    EXPECT_EQ(live.populationGroups[i].id,reloaded.populationGroups[i].id);
    if (i > 0) EXPECT_LT(live.populationGroups[i-1].id,live.populationGroups[i].id);
  }
}

TEST(SaveLoadSystemTests, AllocatorCounterPersistsAndRejectsMalformedFiles) {
  CityMap map({8,8}); RoadNetwork roads(map); EntityStore store; PopulationStore people;
  store.createBuilding(BuildingType::Residential,{2,2},40);
  const auto path = std::filesystem::temp_directory_path()/"urban_allocator_checkpoint.json";
  ASSERT_TRUE(SaveLoadSystem::saveToFile(path.string(),map,roads,store,people));
  json root; { std::ifstream input(path); input >> root; }
  CitySnapshot snapshot;
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(path.string(),snapshot));
  EXPECT_EQ(snapshot.nextEntityId,EntityIdUtils::nextEntityId());
  for (const json& value : {json(0),json(-1),json(1.5),json("2"),json(4294967297ULL),json(1)}) {
    root["nextEntityId"]=value;
    { std::ofstream output(path); output << root; }
    EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(path.string(),snapshot));
  }
  root.erase("nextEntityId");
  { std::ofstream output(path); output << root; }
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(path.string(),snapshot));
  EXPECT_EQ(snapshot.nextEntityId,0u);
  std::filesystem::remove(path);
}

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
  building->crimeRate = 0.12f;
  building->illnessRate = 0.23f;
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
  EXPECT_FLOAT_EQ(loadedBuilding->crimeRate, 0.12f);
  EXPECT_FLOAT_EQ(loadedBuilding->illnessRate, 0.23f);

  EXPECT_GT(loadedRoads.getCongestion({1, 1}, {2, 1}), 0.0f);

  std::filesystem::remove(filePath);
}

TEST(SaveLoadSystemTests, LoadSnapshotFromMissingFileFails) {
  CitySnapshot snapshot;
  const std::filesystem::path missingPath =
    std::filesystem::temp_directory_path() / "urban_sim_core_missing_save_file.json";

  std::filesystem::remove(missingPath);
  SnapshotLoadDiagnostics diagnostics;
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(missingPath.string(), snapshot, &diagnostics));
  EXPECT_TRUE(diagnostics.errorMessage == "unable to open snapshot file");
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
  SnapshotLoadDiagnostics diagnostics;
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot, &diagnostics));
  EXPECT_TRUE(diagnostics.errorMessage == "snapshot schema version is newer than this build supports");

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
  SnapshotLoadDiagnostics diagnostics;
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot, &diagnostics));
  EXPECT_TRUE(diagnostics.errorMessage == "building type is out of range");

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
  SnapshotLoadDiagnostics diagnostics;
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(filePath.string(), snapshot, &diagnostics));
  EXPECT_TRUE(diagnostics.errorMessage == "tile references a missing building ID");

  std::filesystem::remove(filePath);
}

TEST(SaveLoadSystemTests, ApplySnapshotReplacesExistingRoadTopology) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  roads.buildRoad({0, 0}, {1, 0});

  CityMap sourceMap({4, 4});
  RoadNetwork sourceRoads(sourceMap);
  EntityStore sourceStore;
  PopulationStore sourcePopulation;
  sourceRoads.buildRoad({2, 2}, {3, 2});
  const CitySnapshot snapshot = SaveLoadSystem::captureSnapshot(
    sourceMap, sourceRoads, sourceStore, sourcePopulation);

  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot, map, roads, store, population));
  EXPECT_FALSE(roads.hasRoad({0, 0}, {1, 0}));
  EXPECT_TRUE(roads.hasRoad({2, 2}, {3, 2}));
  EXPECT_EQ(roads.getRoadCount(), 1u);
}

TEST(SaveLoadSystemTests, ApplySnapshotReportsDimensionMismatchWithoutMutatingState) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  roads.buildRoad({0, 0}, {1, 0});

  CitySnapshot snapshot;
  snapshot.width = 5;
  snapshot.height = 5;
  std::string error;
  EXPECT_FALSE(SaveLoadSystem::applySnapshot(snapshot, map, roads, store, population, &error));
  EXPECT_TRUE(error == "snapshot dimensions do not match the destination map");
  EXPECT_TRUE(roads.hasRoad({0, 0}, {1, 0}));
}

TEST(SaveLoadSystemTests,
     LegacyBuildingRatesDefaultToZeroAndInvalidRatesRejectLoad) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore population;
  store.createBuilding(BuildingType::Residential, {2, 2}, 40);
  const auto path =
      std::filesystem::temp_directory_path() / "urban_neighborhood_legacy.json";
  ASSERT_TRUE(
      SaveLoadSystem::saveToFile(path.string(), map, roads, store, population));
  json root;
  {
    std::ifstream input(path);
    input >> root;
  }
  root["buildings"][0].erase("crimeRate");
  root["buildings"][0].erase("illnessRate");
  {
    std::ofstream output(path);
    output << root;
  }
  CitySnapshot snapshot;
  SnapshotLoadDiagnostics diagnostics;
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(path.string(), snapshot,
                                                   &diagnostics));
  ASSERT_EQ(snapshot.buildings.size(), 1u);
  EXPECT_FLOAT_EQ(snapshot.buildings[0].crimeRate, 0.0f);
  EXPECT_FLOAT_EQ(snapshot.buildings[0].illnessRate, 0.0f);
  root["buildings"][0]["crimeRate"] = 1.5f;
  {
    std::ofstream output(path);
    output << root;
  }
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(path.string(), snapshot,
                                                    &diagnostics));
  EXPECT_EQ(diagnostics.errorMessage,
            "building neighborhood rates are invalid");
  std::filesystem::remove(path);
}

TEST(SaveLoadSystemTests,
     ArterialAndFreightLoadsSurviveCheckpointAndLegacyRoadsStayLocal) {
  CityMap map({8, 8});
  RoadNetwork roads(map);
  EntityStore store;
  PopulationStore people;
  roads.buildRoad({1, 4}, {2, 4});
  roads.setRoadClass({1, 4}, {2, 4}, RoadClass::Arterial);
  roads.updateCongestion({1, 4}, {2, 4}, 7.0f);
  roads.addFreightLoad({1, 4}, {2, 4}, 3.0f);
  const auto path = std::filesystem::temp_directory_path() /
                    "urban_road_class_checkpoint.json";
  ASSERT_TRUE(
      SaveLoadSystem::saveToFile(path.string(), map, roads, store, people));
  CitySnapshot snapshot;
  SnapshotLoadDiagnostics diagnostics;
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(path.string(), snapshot,
                                                   &diagnostics));
  CityMap loadedMap({8, 8});
  RoadNetwork loadedRoads(loadedMap);
  ASSERT_TRUE(SaveLoadSystem::applySnapshot(snapshot, loadedMap, loadedRoads,
                                            store, people));
  EXPECT_EQ(loadedRoads.getRoadClass({1, 4}, {2, 4}), RoadClass::Arterial);
  EXPECT_FLOAT_EQ(loadedRoads.getEdgeCapacity({1, 4}, {2, 4}), kArterialRoadCapacity);
  EXPECT_FLOAT_EQ(loadedRoads.getFreightLoad({1, 4}, {2, 4}), 3.0f);
  EXPECT_FLOAT_EQ(loadedRoads.getCongestion({1, 4}, {2, 4}), 10.0f / kArterialRoadCapacity);
  json root;
  {
    std::ifstream input(path);
    input >> root;
  }
  root["roads"][0].erase("roadClass");
  root["roads"][0].erase("freightLoad");
  {
    std::ofstream output(path);
    output << root;
  }
  ASSERT_TRUE(SaveLoadSystem::loadSnapshotFromFile(path.string(), snapshot,
                                                   &diagnostics));
  EXPECT_EQ(snapshot.roads[0].roadClass, RoadClass::Local);
  EXPECT_FLOAT_EQ(snapshot.roads[0].freightLoad, 0.0f);
  root["roads"][0]["roadClass"] = 9;
  {
    std::ofstream output(path);
    output << root;
  }
  EXPECT_FALSE(SaveLoadSystem::loadSnapshotFromFile(path.string(), snapshot,
                                                    &diagnostics));
  std::filesystem::remove(path);
}
