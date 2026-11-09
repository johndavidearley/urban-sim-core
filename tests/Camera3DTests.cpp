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

TEST(Camera3D, OrbitAndZoomPreserveTheExactGroundPivotWithoutAccumulatedDrift) {
  Camera3D camera;
  camera.originX=613.25f;camera.originY=-97.125f;camera.scale=31.f;
  const auto anchor=camera.ground(643.7f,387.3f);
  for(int i=0;i<240;++i) {
    camera.orbitAt(643.7f,387.3f,15.f,i%2==0 ? 1.f : -1.f);
    camera.zoomAt(643.7f,387.3f,i%2==0 ? 17.f : 31.f);
  }
  const auto after=camera.ground(643.7f,387.3f);
  EXPECT_NEAR(after.x,anchor.x,.002f);EXPECT_NEAR(after.z,anchor.z,.002f);
  camera.orbit(-2000,0);
  EXPECT_GE(camera.yaw,0);EXPECT_LT(camera.yaw,360);
}

TEST(Camera3D, SolarGeometryCoversItsTwoByTwoFootprintAndTerminalsSurviveWaterTiles) {
  CityMap map({8,8});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  for(int z=0;z<8;++z) for(int x=0;x<8;++x) map.getTile({x,z}).zone=1;
  Camera3D camera;camera.yaw=0;camera.pitch=90;camera.scale=20;
  DrawList bare,solar;
  drawCity3D(&bare,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr);
  ServiceFacility site{ServiceType::Power,{2,2},10,1.f};site.powerSource=PowerSourceType::Solar;
  drawCity3D(&solar,camera,map,roads,store,{site},OverlayMode::Zone,{},0,districts,0,nullptr);
  ASSERT_GT(solar.vertices.size(),bare.vertices.size());
  float right=0,bottom=0;
  for(size_t i=bare.vertices.size();i<solar.vertices.size();++i) {
    const auto& v=solar.vertices[i];
    EXPECT_GE(v.x,40);EXPECT_LE(v.x,80);EXPECT_GE(v.y,39.99f);EXPECT_LE(v.y,80);
    right=std::max(right,v.x);bottom=std::max(bottom,v.y);
  }
  EXPECT_GT(right,75);EXPECT_GT(bottom,75);
  for(int z=0;z<8;++z) for(int x=0;x<8;++x) map.getTile({x,z}).type=2;
  std::vector<regional::Connection> terminals(1);
  terminals[0].kind=regional::Kind::CargoPort;terminals[0].origin={2,2};
  terminals[0].width=2;terminals[0].height=2;
  DrawList dormant,built;
  drawCity3D(&dormant,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr,&terminals);
  terminals[0].constructed=true;
  drawCity3D(&built,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr,&terminals);
  EXPECT_GT(built.vertices.size(),dormant.vertices.size());
  for(const auto& v:built.vertices) { EXPECT_TRUE(std::isfinite(v.x));EXPECT_GT(v.z,0);EXPECT_LT(v.z,1); }
}

TEST(Camera3D, LoweredWaterRemainsVisibleAboveTheSolidMapBase) {
  CityMap map({8,8});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  for(int z=0;z<8;++z) for(int x=0;x<8;++x) map.getTile({x,z}).type=2;
  Camera3D camera;camera.originX=320;camera.originY=150;camera.scale=40;
  DrawList scene;
  drawCity3D(&scene,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr);
  const auto sample=camera.project({4.75f,-.025f,4.75f});
  float nearest=1.f;size_t winner=scene.vertices.size();
  // Resolve the visible triangle at an interior water point using the same
  // interpolated depth comparison as the GPU, including the map's solid base.
  for(size_t i=0;i<scene.vertices.size();i+=3) {
    const auto& a=scene.vertices[i];const auto& b=scene.vertices[i+1];const auto& c=scene.vertices[i+2];
    const float denominator=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
    if(std::abs(denominator)<.00001f) continue;
    const float u=((b.y-c.y)*(sample.x-c.x)+(c.x-b.x)*(sample.y-c.y))/denominator;
    const float v=((c.y-a.y)*(sample.x-c.x)+(a.x-c.x)*(sample.y-c.y))/denominator;
    const float w=1-u-v;
    if(u<-.0001f || v<-.0001f || w<-.0001f) continue;
    const float depth=u*a.z+v*b.z+w*c.z;
    if(depth<=nearest) {nearest=depth;winner=i;}
  }
  ASSERT_LT(winner,scene.vertices.size());
  const auto& visible=scene.vertices[winner];
  EXPECT_NEAR(visible.r,68.f/255.f,.001f);
  EXPECT_NEAR(visible.g,137.f/255.f,.001f);
  EXPECT_NEAR(visible.b,218.f/255.f,.001f);
}

TEST(Camera3D, PerspectiveGroundPickingRoundTripsAcrossOrbitTiltAndZoom) {
  for(float yaw:{0.f,45.f,90.f,180.f,275.f,359.f})
    for(float pitch:{20.f,35.f,60.f,80.f})
      for(float scale:{10.f,20.f,34.f}) {
        Camera3D camera;camera.perspective=true;
        camera.yaw=yaw;camera.pitch=pitch;camera.scale=scale;
        camera.centerOn({16.5f,0,16.5f},640,400);
        for(Coord tile:{Coord{14,14},Coord{16,16},Coord{18,18}}) {
          const auto screen=camera.tileCenter(tile);
          const auto picked=camera.screenToTile(screen);
          EXPECT_EQ(picked.x,tile.x);EXPECT_EQ(picked.y,tile.y);
          const auto p=camera.project({tile.x+.37f,0,tile.y+.61f});
          const auto ground=camera.ground(p.x,p.y);
          ASSERT_TRUE(ground.valid);
          EXPECT_NEAR(ground.x,tile.x+.37f,.001f);EXPECT_NEAR(ground.z,tile.y+.61f,.001f);
        }
      }
}

TEST(Camera3D, PerspectiveForeshortensDistantObjectsAndRejectsSkyPicking) {
  Camera3D camera;camera.perspective=true;camera.yaw=0;
  camera.centerOn({0,0,0},640,400);
  const float nearWidth=camera.project({1,0,8}).x-camera.project({0,0,8}).x;
  const float farWidth=camera.project({1,0,-8}).x-camera.project({0,0,-8}).x;
  EXPECT_GT(nearWidth,farWidth);
  camera.pitch=20;
  EXPECT_FALSE(camera.ground(640,-1000).valid);
  const auto sky=camera.screenToTile({640,-1000});
  EXPECT_EQ(sky.x,-1);EXPECT_EQ(sky.y,-1);
  camera.zoomAt(640,-1000,24); // Sky zoom falls back to the center ground pivot.
  EXPECT_TRUE(camera.ground(640,400).valid);
}

TEST(Camera3D, PerspectiveOrbitAndZoomPreserveFractionalGroundPivot) {
  Camera3D camera;camera.perspective=true;
  camera.centerOn({16.37f,0,14.61f},640,400);
  const auto before=camera.ground(655.3f,427.7f);
  for(int i=0;i<240;++i) {
    camera.orbitAt(655.3f,427.7f,15,i%2==0 ? 1.f : -1.f);
    camera.zoomAt(655.3f,427.7f,i%2==0 ? 17.f : 31.f);
  }
  const auto after=camera.ground(655.3f,427.7f);
  EXPECT_TRUE(after.valid);
  EXPECT_NEAR(before.x,after.x,.003f);EXPECT_NEAR(before.z,after.z,.003f);
}

TEST(Camera3D, PerspectiveNearPlaneGeometryKeepsFiniteHomogeneousCoordinates) {
  CityMap map({8,8});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  Camera3D camera;camera.perspective=true;camera.pitch=20;
  camera.scale=20;camera.focalLength=30;camera.centerX=320;camera.centerY=240;
  camera.centerOn({4,0,4},320,240);
  DrawList scene;
  drawCity3D(&scene,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr);
  bool front=false,behind=false;
  for(const auto& v:scene.vertices) {
    EXPECT_TRUE(std::isfinite(v.x));EXPECT_TRUE(std::isfinite(v.y));
    EXPECT_TRUE(std::isfinite(v.z));EXPECT_TRUE(std::isfinite(v.clipW));
    front |= v.clipW>0;behind |= v.clipW<0;
  }
  EXPECT_TRUE(front);EXPECT_TRUE(behind);
  const size_t highlightStart=scene.vertices.size();
  drawGroundHighlight(&scene,camera,{4,4},{80,220,120},165);
  ASSERT_GT(scene.vertices.size(),highlightStart);
  for(size_t i=highlightStart;i<scene.vertices.size();++i) {
    EXPECT_GE(scene.vertices[i].clipW,Camera3D::nearPlane-.00001f);
    EXPECT_FLOAT_EQ(scene.vertices[i].z,0);
  }
  const size_t uiStart=scene.vertices.size();
  drawFilledRect(&scene,10,10,20,20,{255,0,0});
  for(size_t i=uiStart;i<scene.vertices.size();++i) {
    EXPECT_FLOAT_EQ(scene.vertices[i].clipW,1);EXPECT_FLOAT_EQ(scene.vertices[i].z,0);
  }
}
