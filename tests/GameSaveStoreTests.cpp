#include "gtest/gtest.h"
#include "src/core/EntityId.hpp"
#include "src/persistence/GameSaveStore.hpp"
#include "src/world/CityMap.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>

namespace {
class GameSaveStoreTests:public ::testing::Test {
protected:
  std::filesystem::path root=std::filesystem::temp_directory_path()/
    ("urban-save-store-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  GameSaveStore saves{root};
  CityMap map{{8,8}};
  RoadNetwork roads{map};
  EntityStore entities;
  PopulationStore people;
  GameplaySessionState session;
  std::string error;
  void TearDown() override {std::filesystem::remove_all(root);}
  void save(int64_t funds,const std::string& name="City") {
    session.funds=funds;
    ASSERT_TRUE(saves.save(name,map,roads,entities,people,session,&error))<<error;
  }
  int64_t funds(const std::string& path) {
    GameplaySessionState loaded;
    EXPECT_TRUE(GameplaySessionSystem::load(path,map,roads,entities,people,loaded,&error))<<error;
    return loaded.funds;
  }
};
TEST_F(GameSaveStoreTests, NamedSlotsKeepThreePreviousCitiesAndReclaimOldGenerations) {
  for(int i=0;i<7;++i) save(40000+i);
  for(int i=0;i<=3;++i) EXPECT_EQ(funds(saves.resolve("City",i)),40006-i);
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root/"objects"),std::filesystem::directory_iterator{}),4);
  save(12345,"Harbour Town");
  EXPECT_EQ(funds(saves.resolve("Harbour Town")),12345);
  EXPECT_EQ(funds(saves.resolve("City")),40006);
  save(23456,"Autosave");
  EXPECT_EQ(funds(saves.resolve("Autosave")),23456);
  EXPECT_EQ(funds(saves.resolve("City")),40006);
}
TEST_F(GameSaveStoreTests, FailedValidationCannotOverwriteEitherHalfOfLastGoodCity) {
  save(41000);
  const auto previous=saves.resolve("City");
  ServiceFacility invalid;invalid.type=ServiceType::Sanitation;invalid.sewageTreatment=true;invalid.surfaceWater=false;
  session.facilities.push_back(invalid);
  EXPECT_FALSE(saves.save("City",map,roads,entities,people,session,&error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(saves.resolve("City"),previous);
  EXPECT_EQ(funds(previous),41000);
}
TEST_F(GameSaveStoreTests, InterruptedPublicationAndOrphanedObjectsPreserveLastGoodCity) {
  save(42000);
  const auto previous=saves.resolve("City");
  std::filesystem::create_directories(root/"objects/g-123");
  {std::ofstream partial(root/"objects/g-123/session.json.city.json");partial<<"{partial";}
  std::filesystem::create_directory(root/"City.json.pending");
  session.funds=43000;
  EXPECT_FALSE(saves.save("City",map,roads,entities,people,session,&error));
  EXPECT_EQ(saves.resolve("City"),previous);
  EXPECT_EQ(funds(previous),42000);
  std::filesystem::remove(root/"City.json.pending");
  save(44000);
  EXPECT_EQ(funds(saves.resolve("City")),44000);
  EXPECT_FALSE(std::filesystem::exists(root/"objects/g-123"));
}
TEST_F(GameSaveStoreTests, DamagedPrimaryRecoversBackupAndNeverRotatesDamageIntoIt) {
  save(41000);save(42000);
  const auto damaged=saves.resolve("City");
  {std::ofstream corrupt(damaged+".city.json");corrupt<<"{truncated";}
  bool recovered=false;
  const auto backup=saves.recover("City",recovered,&error);
  ASSERT_FALSE(backup.empty())<<error;
  EXPECT_TRUE(recovered);EXPECT_NE(error.find("backup 1"),std::string::npos);
  EXPECT_EQ(funds(backup),41000);
  save(43000);
  EXPECT_EQ(funds(saves.resolve("City")),43000);
  EXPECT_EQ(funds(saves.resolve("City",1)),41000);
}
TEST_F(GameSaveStoreTests, InvalidManifestAndFutureVersionFailWithoutTouchingLiveCity) {
  save(41000);
  {std::ofstream broken(root/"City.json");broken<<R"({"saveStoreVersion":2,"generation":"g-1"})";}
  EXPECT_TRUE(saves.resolve("City",0,&error).empty());
  EXPECT_NE(error.find("Unsupported"),std::string::npos);
  EXPECT_EQ(session.funds,41000);
  {std::ofstream broken(root/"City.json");broken<<R"({"saveStoreVersion":1,"generation":"../elsewhere"})";}
  EXPECT_TRUE(saves.resolve("City",0,&error).empty());
  EXPECT_NE(error.find("Invalid save generation"),std::string::npos);
}
TEST_F(GameSaveStoreTests, PortableGenerationsResolveBesideTheirManifest) {
  save(41000);
  const auto moved=root/"relocated";
  std::filesystem::create_directory(moved);
  std::filesystem::copy_file(root/"City.json",moved/"City.json");
  std::filesystem::copy(root/"objects",moved/"objects",std::filesystem::copy_options::recursive);
  GameSaveStore relocated(moved);
  EXPECT_EQ(funds(relocated.resolve("City",0,&error)),41000)<<error;
}
TEST_F(GameSaveStoreTests, ManagedGenerationCannotRedirectCoreToAnotherCity) {
  save(41000);
  const auto path=saves.resolve("City");
  nlohmann::json sessionJson;
  {std::ifstream input(path);input>>sessionJson;}
  sessionJson["citySnapshot"]=std::filesystem::absolute(path+".city.json").string();
  {std::ofstream output(path);output<<sessionJson;}
  EXPECT_TRUE(saves.resolve("City",0,&error).empty());
  EXPECT_NE(error.find("invalid core reference"),std::string::npos);
}
TEST_F(GameSaveStoreTests, SavingAndInspectingOldBackupsCannotRewindLiveEntityIds) {
  entities.createBuilding(BuildingType::Residential,{1,1},10);
  save(41000);
  entities.createBuilding(BuildingType::Commercial,{2,1},10);
  const auto next=EntityIdUtils::nextEntityId();
  save(42000);
  EXPECT_EQ(EntityIdUtils::nextEntityId(),next);
  ASSERT_FALSE(saves.resolve("City",1,&error).empty())<<error;
  EXPECT_EQ(EntityIdUtils::nextEntityId(),next);
  const auto added=entities.createBuilding(BuildingType::Industrial,{3,1},10);
  EXPECT_EQ(added,next);
  EXPECT_EQ(entities.getBuildingCount(),3u);
}
TEST_F(GameSaveStoreTests, BrowserListsNamedSavesBackupsAndInvalidRowsWithoutMutatingAllocator) {
  save(41000,"Harbour");session.tick=5;save(42000,"Harbour");save(43000,"Other City");
  const auto path=saves.resolve("Harbour",1);
  {std::ofstream broken(path);broken<<"{invalid";}
  const auto next=EntityIdUtils::nextEntityId();
  const auto rows=saves.entries();
  ASSERT_EQ(rows.size(),3u);
  EXPECT_EQ(rows[0].name,"Harbour");EXPECT_EQ(rows[0].backup,0);EXPECT_EQ(rows[0].tick,5u);EXPECT_TRUE(rows[0].valid);
  EXPECT_EQ(rows[1].name,"Harbour");EXPECT_EQ(rows[1].backup,1);EXPECT_FALSE(rows[1].valid);
  EXPECT_EQ(rows[2].name,"Other City");EXPECT_EQ(rows[2].width,8);EXPECT_EQ(rows[2].height,8);
  EXPECT_EQ(EntityIdUtils::nextEntityId(),next);
}
TEST_F(GameSaveStoreTests, NamesArePortableToWindows) {
  for(const std::string name:{"CON","con","PRN","NUL","AUX","COM1","LPT9"}) EXPECT_FALSE(GameSaveStore::validName(name));
  EXPECT_TRUE(GameSaveStore::validName("Conifer Bay"));EXPECT_TRUE(GameSaveStore::validName("COM10"));
}
TEST_F(GameSaveStoreTests, RejectsUnsafeNamesAndMissingUserLocation) {
  for(const std::string name:{"", "../City", "City/Other", "City.json", " City", "City "})
    EXPECT_FALSE(saves.save(name,map,roads,entities,people,session,&error));
  GameSaveStore missing({});
  EXPECT_FALSE(missing.save("City",map,roads,entities,people,session,&error));
  EXPECT_FALSE(std::filesystem::exists(root));
}
}
