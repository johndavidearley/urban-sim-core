#include "src/visualization/RoadCurves.hpp"
#include "gtest/gtest.h"
#include "src/visualization/Scene3D.hpp"
#include "src/visualization/ScenePicking3D.hpp"
#include "src/visualization/Scene3DPrimitives.hpp"

using namespace visualizer;
namespace {
void expectTile(Coord actual,Coord expected) {EXPECT_EQ(actual.x,expected.x);EXPECT_EQ(actual.y,expected.y);}
}
TEST(ScenePicking3D, RoofGeometryWinsOverTheGroundParcelBehindItInBothProjections) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  const auto id=store.createBuilding(BuildingType::Office,{6,6},18*64);
  map.getTile({6,6}).buildingId=id;
  for(bool perspective:{false,true}) for(float yaw:{0.f,90.f,180.f,270.f}) {
    Camera3D camera;camera.perspective=perspective;camera.yaw=yaw;camera.pitch=20;camera.scale=40;
    camera.centerOn({6.5f,0,6.5f},640,400);
    DrawList scene;ScenePicking3D picking;
    drawCity3D(&scene,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr,nullptr,&picking);
    const auto roof=camera.worldToScreen({6.5f,2.137f,6.5f});
    expectTile(picking.pick(camera,roof),{6,6});
    const auto ground=camera.screenToTile(roof);
    EXPECT_TRUE(ground.x!=6 || ground.y!=6);
    EXPECT_GT(picking.triangleCount(),0u);
    EXPECT_EQ(scene.scenePicking,nullptr); // Collection scope never leaks into HUD drawing.
  }
}
TEST(ScenePicking3D, ExactTrianglesResolveOcclusionAndLeaveHolesUnselectable) {
  Camera3D camera;camera.yaw=0;camera.pitch=90;camera.scale=100;
  const std::array<WorldPoint,3> low{{{0,0,0},{1,0,0},{0,0,1}}};
  const std::array<WorldPoint,3> high{{{0,2,0},{1,2,0},{0,2,1}}};
  for(bool reverse:{false,true}) {
    ScenePicking3D picking;
    picking.addTriangle(reverse ? high : low,reverse ? Coord{4,4} : Coord{2,2});
    picking.addTriangle(reverse ? low : high,reverse ? Coord{2,2} : Coord{4,4});
    expectTile(picking.pick(camera,{20,20}),{4,4});
    expectTile(picking.pick(camera,{80,80}),{-1,-1}); // Inside the AABB, outside the triangle.
    picking.addTriangle({{{0,3,0},{1,3,0},{0,3,1}}},{-1,-1});
    expectTile(picking.pick(camera,{20,20}),{-1,-1}); // Nonselectable geometry still occludes.
  }
}
TEST(ScenePicking3D, FacilitiesAndWholeTerminalsResolveToTheirSiteAnchor) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  ServiceFacility solar{ServiceType::Power,{8,4},10,1};solar.powerSource=PowerSourceType::Solar;
  regional::Connection airport;airport.id=1;airport.kind=regional::Kind::Airport;
  airport.origin={3,10};airport.width=4;airport.height=2;airport.constructed=true;
  std::vector<regional::Connection> terminals{airport};
  for(bool perspective:{false,true}) {
    Camera3D camera;camera.perspective=perspective;camera.scale=40;
    camera.centerOn({8,0,8},640,400);
    DrawList scene;ScenePicking3D picking;
    drawCity3D(&scene,camera,map,roads,store,{solar},OverlayMode::Zone,{},0,districts,0,nullptr,&terminals,&picking);
    expectTile(picking.pick(camera,camera.worldToScreen({9.62f,.25f,5.4f})),{8,4});
    expectTile(picking.pick(camera,camera.worldToScreen({6.59f,.955f,10.28f})),{3,10});
  }
}
TEST(ScenePicking3D, VisibleSkylineCanBeInspectedWhenTheGroundRayPointsIntoTheSky) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  Building spire;spire.id=store.createBuilding(BuildingType::Office,{8,8},0);
  spire.type=BuildingType::Office;spire.position={8,8};spire.monumentKind=3;
  store.upsertBuilding(spire);map.getTile({8,8}).buildingId=spire.id;
  Camera3D camera;camera.perspective=true;camera.pitch=20;camera.focalLength=200;camera.scale=100;
  camera.centerOn({8.5f,0,8.5f},640,400);
  DrawList scene;ScenePicking3D picking;
  drawCity3D(&scene,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr,nullptr,&picking);
  const auto roof=camera.worldToScreen({8.5f,2.24f,8.5f});
  EXPECT_FALSE(camera.ground(float(roof.x),float(roof.y)).valid);
  expectTile(picking.pick(camera,roof),{8,8});
}
TEST(ScenePicking3D, ClippedSurfacesAndConstructionOverlaysDoNotBecomeVisibleObjects) {
  Camera3D camera;camera.perspective=true;camera.yaw=0;camera.focalLength=200;camera.scale=20;
  camera.centerOn({0,0,0},640,400);
  const auto ray=camera.ray(640,400);
  const auto triangleAt=[&](float t) {
    const WorldPoint p{ray.origin.x+t*ray.direction.x,ray.origin.y+t*ray.direction.y,ray.origin.z+t*ray.direction.z};
    return std::array<WorldPoint,3>{{{p.x-.5f,p.y-.5f,p.z},{p.x+.5f,p.y-.5f,p.z},{p.x,p.y+.5f,p.z}}};
  };
  ScenePicking3D picking;
  picking.addTriangle(triangleAt(.1f),{1,1});
  picking.addTriangle(triangleAt(camera.farPlane()+1),{2,2});
  expectTile(picking.pick(camera,{640,400}),{-1,-1});
  picking.addTriangle(triangleAt(5),{3,3});expectTile(picking.pick(camera,{640,400}),{3,3});
  DrawList scene;
  {scene3d::PickCollector collector(&scene,&picking);
    scene3d::PickParcel parcel(&scene,{0,0});drawGroundHighlight(&scene,camera,{0,0},{255,0,0},150);}
  EXPECT_EQ(picking.triangleCount(),0u);
}

TEST(ScenePicking3D, RaisedTerrainRemainsSelectableInBothProjections) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  for(int z=4;z<=10;++z) for(int x=4;x<=10;++x) map.getTile({x,z}).type=1;
  // At the center all four corners are three steps inside the rough region.
  const WorldPoint summit{7.5f,3*.55f+.18f,7.5f};
  for(bool perspective:{false,true}) {
    Camera3D camera;camera.perspective=perspective;camera.scale=60;camera.pitch=60;
    camera.centerOn({7.5f,0,7.5f},640,400);
    DrawList scene;ScenePicking3D picking;
    drawCity3D(&scene,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr,nullptr,&picking);
    expectTile(picking.pick(camera,camera.worldToScreen(summit)),{7,7});
    // Constructing a road cuts a level clearing through the same terrain.
    map.getTile({7,7}).hasRoad=true;
    drawCity3D(&scene,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr,nullptr,&picking);
    expectTile(picking.pick(camera,camera.worldToScreen({7.5f,.012f,7.5f})),{7,7});
    map.getTile({7,7}).hasRoad=false;
  }
}

TEST(ScenePicking3D, HillsUseRoundedProfilesAndShorelinesContainArcGeometry) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  for(int z=4;z<=10;++z) for(int x=4;x<=10;++x) map.getTile({x,z}).type=1;
  map.getTile({2,2}).type=2;
  Camera3D camera;camera.pitch=90;camera.yaw=0;camera.scale=40;
  DrawList scene;ScenePicking3D picking;
  drawCity3D(&scene,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr,nullptr,&picking);
  const auto hasVertex=[&](WorldPoint world) {
    const auto expected=camera.project(world);
    return std::any_of(scene.vertices.begin(),scene.vertices.end(),[&](const auto& vertex) {
      return std::abs(vertex.x-expected.x)<.001f && std::abs(vertex.y-expected.y)<.001f &&
             std::abs(vertex.z-expected.depth)<.000001f;
    });
  };
  // The quarter-tile slope follows a rounded crest rather than a straight
  // line from the edge to the peak. Shared edges retain the original height.
  EXPECT_TRUE(hasVertex({7.25f,3*.55f+.09f,7.5f}));
  EXPECT_TRUE(hasVertex({7.f,3*.55f,7.5f}));
  // The midpoint of a circular bank differs from a square or diagonal cut.
  const float inset=.42f*(1.f-std::sqrt(.5f));
  EXPECT_TRUE(hasVertex({2.f+inset,0,2.f+inset}));
  expectTile(picking.pick(camera,camera.worldToScreen({2.03f,0,2.03f})),{2,2});
  expectTile(picking.pick(camera,camera.worldToScreen({2.5f,-.025f,2.5f})),{2,2});
}

TEST(ScenePicking3D, SplineRoadBendsRemainSelectableInBothProjections) {
  CityMap map({16,16});RoadNetwork roads(map);EntityStore store;DistrictSystem districts;
  for(Coord t:{Coord{7,7},Coord{8,7},Coord{7,8}}) map.getTile(t).hasRoad=true;
  roads.buildRoad({7,7},{8,7});roads.buildRoad({7,7},{7,8});
  for(bool perspective:{false,true}) {
    Camera3D camera;camera.perspective=perspective;camera.scale=80;camera.pitch=60;
    camera.centerOn({7.5f,0,7.5f},640,400);
    DrawList scene;ScenePicking3D picking;
    drawCity3D(&scene,camera,map,roads,store,{},OverlayMode::Zone,{},0,districts,0,nullptr,nullptr,&picking);
    expectTile(picking.pick(camera,camera.worldToScreen({7.64645f,.016f,7.64645f})),{7,7});
  }
}

TEST(RoadCurves, EveryBendHasAlignedTangentsAndStaysInsideItsParcel) {
  for(int dx:{-1,1}) for(int dy:{-1,1}) {
    CityMap map({8,8});RoadNetwork roads(map);
    roads.buildRoad({3,3},{3+dx,3});roads.buildRoad({3,3},{3,3+dy});
    const auto curve=roadCurve(map,&roads,{3,3});
    ASSERT_TRUE(curve.valid);
    const auto start=curve.point(0),end=curve.point(1);
    EXPECT_NEAR(start.x,.5f+.5f*dx,.00001f);EXPECT_FLOAT_EQ(start.y,.5f);
    EXPECT_FLOAT_EQ(end.x,.5f);EXPECT_NEAR(end.y,.5f+.5f*dy,.00001f);
    EXPECT_NEAR(curve.normal(0).x,0,.00001f);
    EXPECT_NEAR(curve.normal(1).y,0,.00001f);
    curve.ribbon(.76f,0,[&](const std::array<glm::vec2,4>& points) {
      for(const auto p:points) {
        EXPECT_GE(p.x,-.00001f);EXPECT_LE(p.x,1.00001f);
        EXPECT_GE(p.y,-.00001f);EXPECT_LE(p.y,1.00001f);
      }
    });
    // A third connection becomes a junction rather than dropping a branch.
    roads.buildRoad({3,3},{3-dx,3});
    EXPECT_FALSE(roadCurve(map,&roads,{3,3}).valid);
    roads.removeRoad({3,3},{3,3+dy});
    EXPECT_FALSE(roadCurve(map,&roads,{3,3}).valid); // Straight-through road.
  }
}
