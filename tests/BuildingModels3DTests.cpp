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
