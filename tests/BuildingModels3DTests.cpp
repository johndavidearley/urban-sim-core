#include "gtest/gtest.h"
#include "src/visualization/BuildingModels3D.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace visualizer;
namespace {
std::vector<Building> examples() {
  std::vector<Building> result;
  for(auto type:{BuildingType::Residential,BuildingType::Commercial,BuildingType::Industrial,BuildingType::Office}) {
    for(int tier:{0,1,2,4,6}) {
      Building b;b.type=type;b.position={3,3};b.capacity=defaultBuildingCapacity(type)*(1<<tier);
      result.push_back(b);
    }
  }
  Building hotel;hotel.type=BuildingType::Commercial;hotel.position={3,3};hotel.hotelBeds=32;
  result.push_back(hotel);hotel.hotelOpen=false;result.push_back(hotel);
  for(uint32_t kind=1;kind<=uniques::count;++kind) {
    Building b;b.type=BuildingType::Office;b.position={3,3};b.uniqueKind=kind;result.push_back(b);
  }
  for(uint32_t kind=1;kind<=4;++kind) {
    Building b;b.type=BuildingType::Office;b.position={3,3};b.monumentKind=kind;result.push_back(b);
  }
  return result;
}
bool sameGeometry(const DrawList& a,const DrawList& b) {
  return a.vertices.size()==b.vertices.size() && std::equal(a.vertices.begin(),a.vertices.end(),b.vertices.begin(),
    [](const Vertex& x,const Vertex& y){return x.x==y.x && x.y==y.y && x.z==y.z && x.clipW==y.clipW;});
}
bool sameMesh(const DrawList& a,const DrawList& b) {
  return sameGeometry(a,b) && std::equal(a.vertices.begin(),a.vertices.end(),b.vertices.begin(),
    [](const Vertex& x,const Vertex& y){return x.r==y.r && x.g==y.g && x.b==y.b && x.a==y.a;});
}
float peak(const Building& b) {
  Camera3D camera;camera.yaw=0;camera.pitch=0;camera.scale=40;
  DrawList list;drawBuilding3D(&list,camera,b,{180,170,150});
  float height=0;
  for(const auto& v:list.vertices) height=std::max(height,-v.y/camera.scale);
  return height;
}
}

TEST(BuildingModels3D, EveryBuildingAndLandmarkStaysInsideItsSimulationParcel) {
  Camera3D camera;camera.yaw=0;camera.pitch=90;camera.scale=40;
  for(const auto& b:examples()) {
    DrawList list;drawBuilding3D(&list,camera,b,{180,170,150});
    ASSERT_FALSE(list.vertices.empty());
    for(const auto& v:list.vertices) {
      EXPECT_GE(v.x,120.f);EXPECT_LE(v.x,160.f);
      EXPECT_GE(v.y,119.99f);EXPECT_LE(v.y,160.f);
      EXPECT_GT(v.z,0);EXPECT_LT(v.z,1);
    }
  }
}
TEST(BuildingModels3D, CapacityGrowthRaisesTheSkylineInEveryOrdinaryCategory) {
  for(auto type:{BuildingType::Residential,BuildingType::Commercial,BuildingType::Industrial,BuildingType::Office}) {
    Building b;b.type=type;b.position={3,3};b.capacity=defaultBuildingCapacity(type);
    float previous=peak(b);
    for(int tier=1;tier<=6;++tier) {
      b.capacity=defaultBuildingCapacity(type)*(1<<tier);
      const float height=peak(b);
      EXPECT_GT(height,previous);previous=height;
    }
  }
}
TEST(BuildingModels3D, EntityIdsAndAnimationTimeDoNotChangeBuildingAppearance) {
  Camera3D camera;camera.originX=640;camera.originY=200;
  for(auto b:examples()) {
    b.id=1;DrawList first;drawBuilding3D(&first,camera,b,{180,170,150});
    b.id=999999;b.occupancy=1234;
    DrawList later;later.frameTimeMs=70000;drawBuilding3D(&later,camera,b,{180,170,150});
    EXPECT_TRUE(sameMesh(first,later));
  }
}
TEST(BuildingModels3D, LandmarkSilhouettesAreDistinctAndDiagnosticColorsKeepTheirGeometry) {
  Camera3D camera;camera.originX=640;camera.originY=200;
  std::vector<DrawList> landmarks;
  for(const auto& b:examples()) {
    DrawList normal,overlay;
    drawBuilding3D(&normal,camera,b,{180,170,150});
    drawBuilding3D(&overlay,camera,b,{230,90,40},true);
    EXPECT_TRUE(sameGeometry(normal,overlay));
    if(b.isCivic()) {
      for(const auto& prior:landmarks) EXPECT_FALSE(sameGeometry(normal,prior));
      landmarks.push_back(normal);
    }
  }
  EXPECT_EQ(landmarks.size(),10u);
}
TEST(BuildingModels3D, ArchitecturalFacesRemainFiniteNearThePerspectiveCamera) {
  Camera3D camera;camera.perspective=true;camera.focalLength=30;camera.scale=20;camera.pitch=20;
  for(float yaw:{0.f,90.f,180.f,270.f}) {
    camera.yaw=yaw;camera.centerOn({3.5f,0,3.5f},640,400);
    for(const auto& b:examples()) {
      DrawList list;drawBuilding3D(&list,camera,b,{180,170,150});
      for(const auto& v:list.vertices) {
        EXPECT_TRUE(std::isfinite(v.x));EXPECT_TRUE(std::isfinite(v.y));
        EXPECT_TRUE(std::isfinite(v.z));EXPECT_TRUE(std::isfinite(v.clipW));
      }
    }
  }
}

#include "src/visualization/CityActivityRender.hpp"

TEST(CityActivity, EmptyCityHasNoInventedPeopleOrTraffic) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;CityActivity activity;
  roads.buildRoad({2,6},{3,6});
  activity.refresh(roads,store,0);
  EXPECT_TRUE(activity.trips().empty());
}

TEST(CityActivity, LoadedConnectedRoadsMovePauseAndClearWithoutSimulationMutation) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;CityActivity activity;
  for(int x=2;x<10;++x) {
    roads.buildRoad({x,6},{x+1,6});roads.updateCongestion({x,6},{x+1,6},64);
    roads.addFreightLoad({x,6},{x+1,6},2);
  }
  const auto home=store.createBuilding(BuildingType::Residential,{4,5},100);
  store.getBuilding(home)->occupancy=75;
  activity.refresh(roads,store,0);
  bool car=false,truck=false,person=false;
  for(const auto& trip:activity.trips()) {
    car|=trip.kind==CityActivity::Kind::Car;truck|=trip.kind==CityActivity::Kind::Truck;
    person|=trip.kind==CityActivity::Kind::Pedestrian;
    for(size_t i=1;i<trip.path.size();++i) EXPECT_TRUE(roads.hasRoad(trip.path[i-1],trip.path[i]));
  }
  EXPECT_TRUE(car);EXPECT_TRUE(truck);EXPECT_TRUE(person);
  const auto before=activity.actors();activity.advance(1000,false);activity.advance(1200,false);
  const auto moving=activity.actors();ASSERT_EQ(before.size(),moving.size());
  EXPECT_NE(before[0].x,moving[0].x);
  const auto firstTrip=activity.trips().front();
  roads.updateCongestion(firstTrip.path[0],firstTrip.path[1],10);
  activity.refresh(roads,store,1);
  EXPECT_NE(firstTrip.speed,activity.trips().front().speed);
  EXPECT_NEAR(moving[0].x,activity.actors()[0].x,.0001f);
  const auto paused=activity.actors();
  activity.advance(1400,true);EXPECT_EQ(paused[0].x,activity.actors()[0].x);
  EXPECT_EQ(store.getBuilding(home)->occupancy,75);
  EXPECT_FLOAT_EQ(roads.getFreightLoad({2,6},{3,6}),2);
  roads.clear();activity.refresh(roads,store,0);EXPECT_TRUE(activity.actors().empty());
}

TEST(CityActivity, VehicleRepresentativesRespectOneWayAndDoNotInterceptPicking) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;CityActivity activity;
  for(int x=2;x<10;++x) {
    roads.buildRoad({x,6},{x+1,6});roads.setOneWay({x,6},{x+1,6},true);
    roads.updateCongestion({x,6},{x+1,6},1000);
  }
  activity.refresh(roads,store,0);
  size_t count=0;
  for(const auto& trip:activity.trips()) {
    if(trip.kind==CityActivity::Kind::Pedestrian) continue;
    ++count;
    for(size_t i=1;i<trip.path.size();++i) EXPECT_TRUE(roads.canTravel(trip.path[i-1],trip.path[i]));
  }
  EXPECT_LE(count,CityActivity::maxCars+CityActivity::maxTrucks);
  DrawList list;ScenePicking3D picking;list.scenePicking=&picking;
  Camera3D camera;camera.originX=100;camera.originY=100;
  drawCityActivity3D(&list,camera,activity.actors());
  EXPECT_EQ(list.scenePicking,&picking);
  EXPECT_FALSE(list.vertices.empty());
  EXPECT_EQ(picking.triangleCount(),0u);
}

TEST(CityActivity, CapsDenseTrafficAndNeverRoutesTrucksOverCommuterOnlyEdges) {
  CityMap map({128,128});RoadNetwork roads(map);EntityStore store;CityActivity activity;
  for(int x=1;x<110;++x) {
    roads.buildRoad({x,6},{x+1,6});roads.updateCongestion({x,6},{x+1,6},1000);
    if(x<80) roads.addFreightLoad({x,6},{x+1,6},2);
  }
  activity.refresh(roads,store,0);
  size_t cars=0,trucks=0;
  for(const auto& trip:activity.trips()) {
    if(trip.kind==CityActivity::Kind::Car) ++cars;
    if(trip.kind==CityActivity::Kind::Truck) {
      ++trucks;
      for(size_t i=1;i<trip.path.size();++i) EXPECT_GT(roads.getFreightLoad(trip.path[i-1],trip.path[i]),0);
    }
  }
  EXPECT_EQ(cars,CityActivity::maxCars);EXPECT_EQ(trucks,CityActivity::maxTrucks);
  CityActivity repeat;repeat.refresh(roads,store,0);
  const auto a=activity.actors(),b=repeat.actors();ASSERT_EQ(a.size(),b.size());
  for(size_t i=0;i<a.size();++i) {EXPECT_EQ(a[i].x,b[i].x);EXPECT_EQ(a[i].z,b[i].z);}
}

TEST(CityActivity, SpecialRoadsUseRenderedCurvesAndDeckHeight) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;CityActivity activity;
  ASSERT_TRUE(roads.buildSeparatedRoad({2,6},{10,6},8,1));
  roads.updateCongestion({2,6},{10,6},20);
  RoadAlignment alignment;alignment.path={{2,6},{10,6}};alignment.elevation=1;
  alignment.roadClass=RoadClass::Highway;
  RoadBezier curve;curve.points={glm::vec2{2.5f,6.5f},glm::vec2{4,3},glm::vec2{8,10},glm::vec2{10.5f,6.5f}};
  alignment.curves.push_back(curve);roads.addAlignment(alignment);
  activity.refresh(roads,store,0);
  ASSERT_EQ(activity.trips().size(),1u);
  EXPECT_FALSE(activity.trips()[0].geometry.empty());
  const auto actor=activity.actors()[0];
  EXPECT_GE(actor.x,2.f);EXPECT_LE(actor.x,11.f);
  EXPECT_GT(actor.y,0);EXPECT_LE(actor.y,.8f);
  EXPECT_NE(actor.dz,0);
}

TEST(CityActivity, HighwayDoesNotInventSidewalkWalkers) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;CityActivity activity;
  roads.buildRoad({2,6},{3,6});roads.setRoadClass({2,6},{3,6},RoadClass::Highway);
  const auto home=store.createBuilding(BuildingType::Residential,{2,5},100);
  store.getBuilding(home)->occupancy=75;
  activity.refresh(roads,store,0);EXPECT_TRUE(activity.trips().empty());
}
