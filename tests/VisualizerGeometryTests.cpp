#include "src/visualization/TransitOverlay.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/visualization/DrawList.hpp"
#include "src/visualization/VisualizerHud.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/WindowHost.hpp"
#include "gtest/gtest.h"
#include <algorithm>
using namespace visualizer;
namespace {
float area(const DrawList &list) {
  float total = 0;
  for (size_t i = 0; i < list.vertices.size(); i += 3) {
    const auto &a = list.vertices[i];
    const auto &b = list.vertices[i + 1];
    const auto &c = list.vertices[i + 2];
    total +=
        std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2.f;
  }
  return total;
}
} // namespace
TEST(VisualizerGeometry, RectangleOrderAndAlphaSurviveBatching) {
  DrawList list;
  list.color = {255, 0, 0, 255};
  list.fill({3, 5, 7, 11});
  list.color = {0, 0, 255, 128};
  list.fill({3, 5, 7, 11});
  ASSERT_EQ(list.vertices.size(), 12u);
  EXPECT_FLOAT_EQ(area(list), 154.f);
  EXPECT_FLOAT_EQ(list.vertices[0].r, 1.f);
  EXPECT_FLOAT_EQ(list.vertices[0].a, 1.f);
  EXPECT_FLOAT_EQ(list.vertices[6].b, 1.f);
  EXPECT_FLOAT_EQ(list.vertices[6].a, 128.f / 255.f);
  list.clear();
  EXPECT_TRUE(list.vertices.empty());
  EXPECT_EQ(list.background.b, 255);
}
TEST(VisualizerGeometry, OutlineDoesNotBlendCornersTwice) {
  DrawList list;
  list.outline({0, 0, 7, 11});
  EXPECT_FLOAT_EQ(area(list), 32.f);
  list.clear();
  list.outline({0, 0, 1, 1});
  EXPECT_FLOAT_EQ(area(list), 1.f);
  list.clear();
  list.fill({0, 0, 0, 8});
  list.outline({0, 0, -1, 8});
  EXPECT_TRUE(list.vertices.empty());
}
TEST(VisualizerGeometry, LinesIncludeEndpointsInBothDirections) {
  DrawList list;
  list.line(2, 3, 6, 7);
  EXPECT_FLOAT_EQ(area(list), 9.f);
  ASSERT_EQ(list.vertices.size(), 12u);
  EXPECT_FLOAT_EQ(list.vertices[0].x, 2.f);
  EXPECT_FLOAT_EQ(list.vertices[2].x, 7.f);
  list.clear();
  list.line(6, 7, 2, 3);
  EXPECT_FLOAT_EQ(area(list), 9.f);
  EXPECT_FLOAT_EQ(list.vertices[0].x, 2.f);
  list.clear();
  list.line(2, 3, 2, 3);
  EXPECT_FLOAT_EQ(area(list), 1.f);
}
TEST(VisualizerGeometry, GlyphCaseSpacingAndLargeTextArePreserved) {
  DrawList upper, lower;
  drawGlyph(&upper, 0, 0, 'A', {255, 255, 255}, 2);
  drawGlyph(&lower, 0, 0, 'a', {255, 255, 255}, 2);
  ASSERT_EQ(upper.vertices.size(), lower.vertices.size());
  for (size_t i = 0; i < upper.vertices.size(); ++i) {
    EXPECT_EQ(upper.vertices[i].x, lower.vertices[i].x);
    EXPECT_EQ(upper.vertices[i].y, lower.vertices[i].y);
  }
  const bool saved = gReadableUiText;
  gReadableUiText = false;
  DrawList small;
  drawText(&small, 0, 0, "A A", {255, 255, 255}, 1);
  gReadableUiText = true;
  DrawList large;
  drawText(&large, 0, 0, "A A", {255, 255, 255}, 1);
  gReadableUiText = saved;
  EXPECT_FLOAT_EQ(area(large), area(small) * 4.f);
  EXPECT_FLOAT_EQ(large.vertices[upper.vertices.size()].x - large.vertices[0].x,
                  12.f);
}
TEST(VisualizerGeometry, LogicalHitTestsIgnoreFramebufferScale) {
  const auto button = paletteButtonRect(0, 1280, 800);
  EXPECT_EQ(paletteHitTest(button.x, button.y, 1280, 800), PaletteTool::Road);
  EXPECT_EQ(paletteHitTest(button.x + button.w, button.y, 1280, 800),
            PaletteTool::None);
  const auto resized = paletteButtonRect(0, 1100, 720);
  EXPECT_EQ(button.x - resized.x, 90);
  EXPECT_EQ(button.y - resized.y, 80);
}

TEST(VisualizerGeometry, DiamondUsesConstantGeometryAndExactProjectedArea) {
  DrawList list;
  drawDiamond(&list, {100, 20}, 32, 16, {100, 150, 200}, 128);
  ASSERT_EQ(list.vertices.size(), 6u);
  EXPECT_FLOAT_EQ(area(list), 256.f);
  EXPECT_FLOAT_EQ(list.vertices[0].x, 100.f);
  EXPECT_FLOAT_EQ(list.vertices[0].y, 20.f);
  EXPECT_FLOAT_EQ(list.vertices[2].y, 36.f);
  EXPECT_FLOAT_EQ(list.vertices[0].a, 128.f / 255.f);
  list.clear();
  drawDiamond(&list, {0, 0}, 320, 160, {100, 150, 200});
  EXPECT_EQ(list.vertices.size(), 6u);
  EXPECT_FLOAT_EQ(area(list), 25600.f);
  list.clear();
  drawDiamond(&list, {0, 0}, 0, 16, {100, 150, 200});
  EXPECT_TRUE(list.vertices.empty());
}

TEST(VisualizerGeometry, IsometricWallsMeetRoofAndGroundAtProjectedCorners) {
  DrawList list;
  drawIsometricObject(&list, {100, 20}, 32, 16, {200, 160, 120}, 24);
  // Shadow, left wall, right wall, and roof each have two triangles.
  ASSERT_GE(list.vertices.size(), 24u);
  EXPECT_FLOAT_EQ(list.vertices[6].x, 88.f);
  EXPECT_FLOAT_EQ(list.vertices[6].y, 4.f);
  EXPECT_FLOAT_EQ(list.vertices[7].x, 100.f);
  EXPECT_FLOAT_EQ(list.vertices[7].y, 9.f);
  EXPECT_FLOAT_EQ(list.vertices[8].y, 33.f);
  EXPECT_FLOAT_EQ(list.vertices[12].x, list.vertices[7].x);
  EXPECT_FLOAT_EQ(list.vertices[12].y, list.vertices[7].y);
  EXPECT_FLOAT_EQ(list.vertices[19].x, 112.f);
  EXPECT_FLOAT_EQ(list.vertices[19].y, 4.f);
  EXPECT_GT(list.vertices[6].r, list.vertices[12].r);
  EXPECT_GT(list.vertices[18].r, list.vertices[6].r);
}

TEST(VisualizerGeometry, IsometricRoadsUseNetworkEdgesAndStayInsideParcel) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  const std::vector<ServiceFacility> facilities;
  const std::unordered_map<Coord, float, Vec2Hash> heat;
  const IsometricProjection projection(64, 32, {100, 100});
  map.getTile({1, 1}).hasRoad = true;
  DrawList isolated, adjacent, connected;
  const auto draw = [&](DrawList& list) {
    drawIsometricTile(&list, map, roads, store, facilities, heat, 0.f,
                      OverlayMode::Zone, projection, {1, 1});
  };
  draw(isolated);
  // A neighboring road tile without an edge must not create a connection.
  map.getTile({2, 1}).hasRoad = true;
  draw(adjacent);
  ASSERT_EQ(isolated.vertices.size(), adjacent.vertices.size());
  for (size_t i = 0; i < isolated.vertices.size(); ++i) {
    EXPECT_EQ(isolated.vertices[i].x, adjacent.vertices[i].x);
    EXPECT_EQ(isolated.vertices[i].y, adjacent.vertices[i].y);
  }
  roads.buildRoad({1, 1}, {2, 1});
  draw(connected);
  EXPECT_GT(connected.vertices.size(), adjacent.vertices.size());
  const auto top = projection.tileTop({1, 1});
  for (const auto& vertex : connected.vertices) {
    EXPECT_GE(vertex.x, top.x - 32);
    EXPECT_LE(vertex.x, top.x + 33);
    EXPECT_GE(vertex.y, top.y);
    EXPECT_LE(vertex.y, top.y + 33);
  }
}

TEST(VisualizerGeometry, CivicLandmarksRemainFiniteAtEverySupportedZoom) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  const std::unordered_map<Coord, float, Vec2Hash> heat;
  for (int width : {14, 22, 32, 64, 128}) {
    const IsometricProjection projection(width, width / 2, {100, 100});
    for (size_t service = 0; service < kServiceTypeCount; ++service) {
      const std::vector<ServiceFacility> facilities = {
          {static_cast<ServiceType>(service), {1, 1}, 10, 1.f}};
      DrawList list;
      drawIsometricTile(&list, map, roads, store, facilities, heat, 0.f,
                        OverlayMode::Zone, projection, {1, 1});
      EXPECT_GT(list.vertices.size(), 6u);
      EXPECT_EQ(list.vertices.size() % 3, 0u);
      for (const auto& vertex : list.vertices) {
        EXPECT_TRUE(std::isfinite(vertex.x));
        EXPECT_TRUE(std::isfinite(vertex.y));
        EXPECT_GE(vertex.a, 0.f);
        EXPECT_LE(vertex.a, 1.f);
      }
      DrawList overlay;
      drawIsometricTile(&overlay, map, roads, store, facilities, heat, 0.f,
                        OverlayMode::LandValue, projection, {1, 1});
      DrawList plainOverlay;
      drawIsometricTile(&plainOverlay, map, roads, store, {}, heat, 0.f,
                        OverlayMode::LandValue, projection, {1, 1});
      EXPECT_EQ(overlay.vertices.size(), plainOverlay.vertices.size());
    }
  }
}

TEST(VisualizerGeometry, DistrictBoundariesAreThinAndPrecedeBuildings) {
  CityMap map({4, 4});
  EntityStore store;
  RoadNetwork roads(map);
  DistrictSystem districts;
  districts.createDistrict("Core", {1, 1}, {2, 2});
  const IsometricProjection projection(64, 32, {100, 100});
  const std::unordered_map<Coord, float, Vec2Hash> heat;
  map.getTile({1, 1}).buildingId = store.createBuilding(BuildingType::Office, {1, 1}, 64);
  DrawList plain, marked;
  drawIsometricTile(&plain, map, roads, store, {}, heat, 0.f,
                    OverlayMode::Zone, projection, {1, 1});
  drawIsometricTile(&marked, map, roads, store, {}, heat, 0.f,
                    OverlayMode::Zone, projection, {1, 1}, &districts);
  const size_t added = marked.vertices.size() - plain.vertices.size();
  ASSERT_EQ(added, 24u); // Two one-pixel boundary segments.
  EXPECT_LT(area(marked) - area(plain), 150.f);
  // All building geometry remains the same suffix, after the boundary layer.
  const size_t buildingVertices = 24;
  for (size_t offset = 1; offset <= buildingVertices; ++offset) {
    const auto& a = plain.vertices[plain.vertices.size() - offset];
    const auto& b = marked.vertices[marked.vertices.size() - offset];
    EXPECT_EQ(a.x, b.x);
    EXPECT_EQ(a.y, b.y);
    EXPECT_EQ(a.r, b.r);
    EXPECT_EQ(a.a, b.a);
  }
}

TEST(VisualizerGeometry, OccupancyDoesNotChangeBuildingSilhouette) {
  CityMap map({4, 4});
  RoadNetwork roads(map);
  EntityStore store;
  const Coord coord{1, 1};
  const auto id = store.createBuilding(BuildingType::Residential, coord, 100);
  map.getTile(coord).buildingId = id;
  const IsometricProjection projection(64, 32, {100, 100});
  const std::unordered_map<Coord, float, Vec2Hash> heat;
  DrawList lowIso, highIso, lowTop, highTop;
  store.getBuilding(id)->occupancy = 25;
  drawIsometricTile(&lowIso, map, roads, store, {}, heat, 0.f,
                    OverlayMode::Zone, projection, coord);
  drawNormalTileDetail(&lowTop, map, store, {}, coord, 0, 0, 32);
  store.getBuilding(id)->occupancy = 90;
  drawIsometricTile(&highIso, map, roads, store, {}, heat, 0.f,
                    OverlayMode::Zone, projection, coord);
  drawNormalTileDetail(&highTop, map, store, {}, coord, 0, 0, 32);
  for (const auto& pair : {std::pair<const DrawList*, const DrawList*>{&lowIso, &highIso},
                          {&lowTop, &highTop}}) {
    ASSERT_EQ(pair.first->vertices.size(), pair.second->vertices.size());
    for (size_t i = 0; i < pair.first->vertices.size(); ++i) {
      EXPECT_EQ(pair.first->vertices[i].x, pair.second->vertices[i].x);
      EXPECT_EQ(pair.first->vertices[i].y, pair.second->vertices[i].y);
    }
  }
}

TEST(VisualizerGeometry, ParkHappinessTapersWithoutStackingAndBothViewsHaveDetail) {
  CityMap map({12, 12}); RoadNetwork roads(map);
  const float base = happinessAtTile(map, roads, {3, 3}, {}, 0.f);
  map.setZone({4, 3}, static_cast<int>(ZoneType::Park));
  EXPECT_NEAR(happinessAtTile(map, roads, {3, 3}, {}, 0.f) - base, 0.06f, 1e-6);
  map.setZone({2, 3}, static_cast<int>(ZoneType::Park));
  EXPECT_NEAR(happinessAtTile(map, roads, {3, 3}, {}, 0.f) - base, 0.06f, 1e-6);
  EXPECT_FLOAT_EQ(happinessAtTile(map, roads, {9, 9}, {}, 0.f), base);
  EntityStore store; DrawList top, iso;
  drawNormalTileDetail(&top, map, store, {}, {4, 3}, 0, 0, 32);
  drawIsometricTile(&iso, map, roads, store, {}, {}, 0.f, OverlayMode::Zone,
                    IsometricProjection(32, 16), {4, 3});
  EXPECT_GT(top.vertices.size(), 30u);
  EXPECT_GT(iso.vertices.size(), 30u);
}

TEST(VisualizerGeometry, TransitFollowsRoadsAndInvalidatesAfterDemolition) {
  CityMap map({16, 16});
  RoadNetwork roads(map);
  roads.buildRoad({2, 2}, {2, 3});
  roads.buildRoad({2, 3}, {3, 3});
  roads.buildRoad({3, 3}, {4, 3});
  TransitRoute route;
  route.stops = {{2, 2}, {4, 3}};
  TransitOverlayCache cache;
  const auto& marks = cache.update(roads, {route});
  ASSERT_EQ(marks.count({3, 3}), 1u);
  EXPECT_EQ(marks.count({3, 2}), 0u);
  DrawList list;
  drawTransitTile(&list, marks, {2, 2}, {100, 100}, 32, 16, true);
  EXPECT_GT(area(list), 0.f);
  list.clear();
  drawTransitTile(&list, marks, {2, 2}, {100, 100}, 28, 28, false);
  EXPECT_GT(area(list), 0.f);
  roads.removeRoad({2, 3}, {3, 3});
  const auto& disconnected = cache.update(roads, {route});
  EXPECT_EQ(disconnected.count({3, 3}), 0u);
  EXPECT_EQ(disconnected.size(), 2u); // Only stop markers remain.
  route.mode = TransitMode::Rail;
  const auto& rail = cache.update(roads, {route});
  EXPECT_EQ(rail.at({2, 2}).front().mode, TransitMode::Rail);
  list.clear();
  drawTransitTile(&list, rail, {2, 2}, {100, 100}, 32, 16, true);
  EXPECT_GT(area(list), 0.f);
  EXPECT_TRUE(cache.update(roads, {}).empty());
}

TEST(VisualizerGeometry, TransitOverlayHasReachableNinthButton) {
  ASSERT_EQ(overlayModes().size(), 9u);
  OverlayMode mode = OverlayMode::Zone;
  EXPECT_TRUE(overlayHitTest(kOverlayKeyStartX + 8 * (kOverlayKeyWidth + kOverlayKeyGap) + 1,
                             kOverlayKeyY + 1, mode));
  EXPECT_EQ(mode, OverlayMode::Transit);
  EXPECT_STREQ(overlayModeName(mode), "transit");
}

TEST(VisualizerGeometry, BudgetControlsStayInsideTheMinimumWindow) {
  const auto panel = budgetPanelRect(800, 640);
  EXPECT_GE(panel.x, 0);
  EXPECT_GE(panel.y, 0);
  EXPECT_LE(panel.x + panel.w, 800);
  EXPECT_LE(panel.y + panel.h, 640);
  for (int action = 0; action < 12; ++action) {
    const auto control = budgetControlRect(action, 800, 640);
    EXPECT_GE(control.x, panel.x);
    EXPECT_GE(control.y, panel.y);
    EXPECT_LE(control.x + control.w, panel.x + panel.w);
    EXPECT_LE(control.y + control.h, panel.y + panel.h);
    EXPECT_EQ(budgetHitTest(control.x + 1, control.y + 1, 800, 640), action);
  }
  EXPECT_EQ(budgetHitTest(0, 0, 800, 640), -1);
  DrawList list;
  BudgetPolicy policy;
  policy.taxPercent[0] = 20;
  drawBudgetPanel(&list, 800, 640, policy, {}, {}, 50000, 0, 0, 0);
  EXPECT_GT(area(list), 0.f);
}

TEST(VisualizerGeometry, ServiceFundingControlsFitTheMinimumWindowAndTabsDoNotOverlap) {
  const auto panel = budgetPanelRect(800,640);
  for (int action = 0; action < 36; ++action) {
    if (action >= 2 && action < 12) continue;
    const auto r = budgetControlRect(action,800,640,true);
    EXPECT_GE(r.x, panel.x);
    EXPECT_GE(r.y, panel.y);
    EXPECT_LE(r.x+r.w, panel.x+panel.w);
    EXPECT_LE(r.y+r.h, panel.y+panel.h);
    EXPECT_EQ(budgetHitTest(r.x+1,r.y+1,800,640,true),action);
  }
  DrawList list;
  drawBudgetPanel(&list,800,640,{}, {}, {},50000,0,0,0,true);
  EXPECT_GT(area(list),0.f);
}

TEST(VisualizerGeometry, TrendsTabHidesPolicyInputsAndFitsMinimumWindow) {
  for (int action = 0; action < 37; ++action) {
    if (!budgetControlVisible(action, false, true)) continue;
    EXPECT_TRUE(action == 0 || action == 12 || action == 13 || action == 36);
    const auto r = budgetControlRect(action, 800, 640, false, true);
    EXPECT_EQ(budgetHitTest(r.x+1,r.y+1,800,640,false,true), action);
  }
  CityHistory history;
  history.append({1, 100, {0,5,0,-5,0}, 40,50,1000,0,{}});
  history.append({2, 105, {5,0,0,5,0}, 60,30,1100,0,{}});
  DrawList list;
  drawBudgetPanel(&list,800,640,{}, {}, {},1100,0,0,0,false,{}, {},true,history);
  EXPECT_GT(area(list), 0.f);
}

TEST(VisualizerGeometry, AlertActionsFitAndHidePolicyControls) {
  for (int action = 0; action < 43; ++action) {
    if (!budgetControlVisible(action,false,false,true)) continue;
    EXPECT_TRUE(action == 0 || action == 12 || action == 13 || action >= 36);
    const auto r = budgetControlRect(action,800,640,false,false,true);
    EXPECT_EQ(budgetHitTest(r.x+1,r.y+1,800,640,false,false,true),action);
    EXPECT_GE(r.x,0); EXPECT_GE(r.y,0);
    EXPECT_LE(r.x+r.w,800); EXPECT_LE(r.y+r.h,640);
  }
  DrawList list;
  drawBudgetPanel(&list,800,640,{}, {}, {},50000,0,0,0,false,{}, {},false,{},true,{});
  EXPECT_GT(area(list),0.f);
}
