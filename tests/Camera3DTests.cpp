#include "gtest/gtest.h"
#include "src/visualization/Scene3D.hpp"
#include <cmath>

using namespace visualizer;
TEST(Camera3D, GroundPickingRoundTripsAcrossOrbitTiltZoomAndPan) {
  for(float yaw:{0.f,45.f,90.f,180.f,275.f,359.f})
    for(float pitch:{20.f,35.f,60.f,80.f})
      for(float scale:{4.f,20.f,34.f}) {
        Camera3D camera;
        camera.yaw=yaw;camera.pitch=pitch;camera.scale=scale;
        camera.originX=721.f;camera.originY=-93.f;
        for(Coord tile:{Coord{0,0},Coord{31,17},Coord{95,95},Coord{-3,4}}) {
          const auto screen=camera.tileCenter(tile);
          const auto picked=camera.screenToTile(screen);
          EXPECT_EQ(picked.x,tile.x);EXPECT_EQ(picked.y,tile.y);
        }
      }
}
TEST(Camera3D, HeightProjectsUpAndCloserAndOrbitStaysBounded) {
  Camera3D camera;
  const auto ground=camera.project({4,0,4}), roof=camera.project({4,2,4});
  EXPECT_LT(roof.y,ground.y);EXPECT_LT(roof.depth,ground.depth);
  camera.orbit(-90,-100);EXPECT_FLOAT_EQ(camera.yaw,315);EXPECT_FLOAT_EQ(camera.pitch,20);
  camera.orbit(400,200);EXPECT_FLOAT_EQ(camera.yaw,355);EXPECT_FLOAT_EQ(camera.pitch,80);
}
TEST(Camera3D, CityMeshCarriesDepthAndHighlightsStayAboveWorld) {
  CityMap map({8,8});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  Camera3D camera;camera.originX=640;camera.originY=200;
  DrawList list;
  const auto id=store.createBuilding(BuildingType::Residential,{3,3},128);
  map.getTile({3,3}).buildingId=id;
  drawCity3D(&list,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr);
  ASSERT_FALSE(list.vertices.empty());
  float minDepth=1.f,maxDepth=0.f;
  for(const auto& v:list.vertices) {
    EXPECT_TRUE(std::isfinite(v.x));EXPECT_TRUE(std::isfinite(v.y));
    EXPECT_GT(v.z,0.f);EXPECT_LT(v.z,1.f);
    minDepth=std::min(minDepth,v.z);maxDepth=std::max(maxDepth,v.z);
  }
  EXPECT_GT(maxDepth-minDepth,.01f);
  const auto count=list.vertices.size();
  drawGroundHighlight(&list,camera,{3,3},{80,220,120},165);
  ASSERT_EQ(list.vertices.size(),count+6);
  for(size_t i=count;i<list.vertices.size();++i) EXPECT_FLOAT_EQ(list.vertices[i].z,0.f);
}
