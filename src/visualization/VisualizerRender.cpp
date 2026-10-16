#include "src/visualization/TransitOverlay.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/visualization/DrawList.hpp"

#include "src/entities/EntityStore.hpp"
#include "src/entities/PopulationStore.hpp"
#include "src/gameplay/BulldozeTool.hpp"
#include "src/gameplay/RoadTool.hpp"
#include "src/gameplay/ServiceTool.hpp"
#include "src/gameplay/TreasurySystem.hpp"
#include "src/gameplay/ZoneTool.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/systems/EconomySystem.hpp"
#include "src/systems/DeathcareSystem.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/systems/PopulationSystem.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/systems/TrafficSystem.hpp"
#include "src/systems/WasteSystem.hpp"
#include "src/visualization/IsometricProjection.hpp"
#include "src/world/CityMap.hpp"
#include "src/world/TerrainGenerator.hpp"
#include "src/world/Zoning.hpp"
#include "src/systems/ParkSystem.hpp"

#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/VisualizerOverlay.hpp"


namespace visualizer {
void drawFireMarker(DrawList* renderer, Point center, int size) {
  const int radius = std::max(3, size / 6);
  setDrawColor(renderer, 245, 90, 25, 255);
  renderer->polygon({{center.x - radius, center.y},
                     {center.x - radius / 2, center.y - radius * 2},
                     {center.x + radius, center.y},
                     {center.x, center.y + radius / 2}});
  setDrawColor(renderer, 255, 220, 70, 255);
  renderer->triangle(static_cast<float>(center.x - radius / 2), static_cast<float>(center.y),
                      static_cast<float>(center.x), static_cast<float>(center.y - radius),
                      static_cast<float>(center.x + radius / 2), static_cast<float>(center.y));
}


void drawFilledRect(DrawList* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha) {
  setDrawColor(renderer, color.r, color.g, color.b, alpha);
  Rect rect{x, y, w, h};
  fillRect(renderer, &rect);
}

void drawRectOutline(DrawList* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha) {
  setDrawColor(renderer, color.r, color.g, color.b, alpha);
  Rect rect{x, y, w, h};
  outlineRect(renderer, &rect);
}

uint32_t visualHash(Coord coord) {
  uint32_t value = static_cast<uint32_t>(coord.x) * 0x9e3779b9u;
  value ^= static_cast<uint32_t>(coord.y) * 0x85ebca6bu;
  value ^= value >> 16;
  value *= 0x7feb352du;
  return value ^ (value >> 15);
}

bool hasRoadAt(const CityMap& map, Coord coord) {
  return map.isValid(coord) && map.getTile(coord).hasRoad;
}

RGB buildingFacade(BuildingType type) {
  switch (type) {
    case BuildingType::Residential: return {218, 190, 151};
    case BuildingType::Commercial: return {91, 148, 186};
    case BuildingType::Industrial: return {166, 133, 91};
    case BuildingType::Office: return {133, 142, 174};
    default: return {175, 175, 175};
  }
}

void drawNormalTileDetail(DrawList *renderer, const CityMap &map,
                          const EntityStore &store,
                          const std::vector<ServiceFacility> &facilities,
                          Coord coord, int x, int y, int size,
                          const RoadNetwork *roads) {
  const Tile& tile = map.getTile(coord);
  const uint32_t variation = visualHash(coord);

  if (tile.type == 2) {
    if (size >= 6) {
      const int bandY = y + 2 + static_cast<int>((variation + renderer->frameTimeMs / 180u) % std::max(2, size - 3));
      drawFilledRect(renderer, x + 1, bandY, std::max(1, size - 2), 1, {145, 190, 250}, 120);
    }
    return;
  }

  if (ParkSystem::isPark(map, coord)) {
    drawFilledRect(renderer, x + 1, y + 1, size - 2, size - 2, {77, 155, 87});
    if (size >= 10) {
      const int path = std::max(2, size / 7);
      drawFilledRect(renderer, x, y + size / 2 - path / 2, size, path, {211, 198, 156});
      drawFilledRect(renderer, x + size / 2 - path / 2, y, path, size, {211, 198, 156});
      drawFilledRect(renderer, x + size / 6, y + size * 2 / 3, std::max(3, size / 4), 2, {136, 91, 54});
      for (int offset : {size / 5, size * 3 / 5}) {
        const int crown = std::max(3, size / 5);
        drawFilledRect(renderer, x + offset + 1, y + offset + 2, crown, crown, {31, 76, 45}, 100);
        drawFilledRect(renderer, x + offset, y + offset, crown, crown, {45, 111, 61});
        drawFilledRect(renderer, x + offset, y + offset, crown - 1, std::max(1, crown / 2), {93, 181, 92});
      }
    }
    return;
  }

  if (size >= 6) {
    const RGB shore{218, 210, 158};
    if (map.isValid({coord.x, coord.y - 1}) && map.getTile({coord.x, coord.y - 1}).type == 2) {
      drawFilledRect(renderer, x, y, size, std::max(1, size / 8), shore, 220);
    }
    if (map.isValid({coord.x, coord.y + 1}) && map.getTile({coord.x, coord.y + 1}).type == 2) {
      drawFilledRect(renderer, x, y + size - std::max(1, size / 8), size, std::max(1, size / 8), shore, 220);
    }
    if (map.isValid({coord.x - 1, coord.y}) && map.getTile({coord.x - 1, coord.y}).type == 2) {
      drawFilledRect(renderer, x, y, std::max(1, size / 8), size, shore, 220);
    }
    if (map.isValid({coord.x + 1, coord.y}) && map.getTile({coord.x + 1, coord.y}).type == 2) {
      drawFilledRect(renderer, x + size - std::max(1, size / 8), y, std::max(1, size / 8), size, shore, 220);
    }
  }

  if (!tile.hasRoad && tile.buildingId == 0 && size >= 8) {
    const RGB speck = tile.type == 1 ? RGB{150, 157, 145} : RGB{193, 208, 183};
    const int dotX = x + 2 + static_cast<int>(variation % static_cast<uint32_t>(std::max(1, size - 4)));
    const int dotY = y + 2 + static_cast<int>((variation >> 8) % static_cast<uint32_t>(std::max(1, size - 4)));
    drawFilledRect(renderer, dotX, dotY, size >= 18 ? 2 : 1, size >= 18 ? 2 : 1, speck, 150);
    if (tile.zone == 0 && !tile.regionalConnectionId && tile.type == 0 && size >= 14 && variation % 11u == 0u) {
      const int crown = std::max(3, size / 4);
      const int treeX = x + 2 + static_cast<int>((variation >> 12) % static_cast<uint32_t>(std::max(1, size - crown - 3)));
      const int treeY = y + 2 + static_cast<int>((variation >> 20) % static_cast<uint32_t>(std::max(1, size - crown - 4)));
      drawFilledRect(renderer, treeX + crown / 2, treeY + crown - 1, 2, 3, {105, 78, 46});
      drawFilledRect(renderer, treeX, treeY + 1, crown, crown, {54, 123, 66});
      drawFilledRect(renderer, treeX + 1, treeY, std::max(2, crown - 1), crown, {72, 151, 79});
    }
  }

  if (tile.hasRoad) {
    bool arterial = false;
    if (roads)
      for (Coord delta : {Coord{1, 0}, Coord{-1, 0}, Coord{0, 1}, Coord{0, -1}})
        arterial |= roads->hasRoad(
                        coord, Coord{coord.x + delta.x, coord.y + delta.y}) &&
                    roads->getRoadClass(
                        coord, Coord{coord.x + delta.x, coord.y + delta.y}) ==
                        RoadClass::Arterial;
    const int roadWidth = std::max(4, size * (arterial ? 4 : 3) / 5);
    const int half = roadWidth / 2;
    const int centerX = x + size / 2;
    const int centerY = y + size / 2;
    const bool north = hasRoadAt(map, {coord.x, coord.y - 1});
    const bool south = hasRoadAt(map, {coord.x, coord.y + 1});
    const bool west = hasRoadAt(map, {coord.x - 1, coord.y});
    const bool east = hasRoadAt(map, {coord.x + 1, coord.y});

    drawFilledRect(renderer, centerX - half, centerY - half, roadWidth, roadWidth, {58, 61, 65});
    if (north) drawFilledRect(renderer, centerX - half, y, roadWidth, size / 2 + 1, {58, 61, 65});
    if (south) drawFilledRect(renderer, centerX - half, centerY, roadWidth, size - size / 2, {58, 61, 65});
    if (west) drawFilledRect(renderer, x, centerY - half, size / 2 + 1, roadWidth, {58, 61, 65});
    if (east) drawFilledRect(renderer, centerX, centerY - half, size - size / 2, roadWidth, {58, 61, 65});

    if (size >= 14) {
      const RGB marking{225, 198, 94};
      if (north || south) drawFilledRect(renderer, centerX, y + 2, 1, std::max(1, size - 4), marking, 190);
      if (west || east) drawFilledRect(renderer, x + 2, centerY, std::max(1, size - 4), 1, marking, 190);
    }
  }

  if (tile.buildingId != 0) {
    const Building* building = store.getBuilding(tile.buildingId);
    if (building != nullptr) {
      const int tier = buildingDensityTier(building->type, building->capacity);
      const int margin = std::max(1, std::max(2, size / 7) - std::min(tier, 3));
      const int lift = size >= 18 ? 3 : 1;
      const int width = std::max(3, size - margin * 2);
      const int height = std::max(3, size - margin * 2 - lift);
      const RGB facade = buildingFacade(building->type);
      drawFilledRect(renderer, x + margin + 2, y + margin + lift + 2, width, height, {48, 51, 54}, 110);
      drawFilledRect(renderer, x + margin, y + margin + lift, width, height, facade);
      drawFilledRect(renderer, x + margin, y + margin, width, lift + 2, {
        static_cast<uint8_t>(std::min(255, facade.r + 28)),
        static_cast<uint8_t>(std::min(255, facade.g + 28)),
        static_cast<uint8_t>(std::min(255, facade.b + 28))
      });
      // One stripe per capacity doubling, so a 2× building and a 16×
      // building are different heights in the top-down view.
      const int stripe = std::max(1, size / 8);
      const int towerW = std::max(2, width * 2 / 3);
      const int towerX = x + margin + (width - towerW) / 2;
      const RGB tower{
        static_cast<uint8_t>(std::min(255, facade.r + 36)),
        static_cast<uint8_t>(std::min(255, facade.g + 36)),
        static_cast<uint8_t>(std::min(255, facade.b + 36))
      };
      for (int floor = 0; floor < tier; ++floor) {
        drawFilledRect(
          renderer, towerX, y + margin + lift - (floor + 1) * stripe, towerW, stripe, tower);
      }
      if (size >= 16) {
        const RGB window{205, 229, 225};
        for (int wx = x + margin + 2; wx < x + margin + width - 1; wx += 4) {
          drawFilledRect(renderer, wx, y + margin + lift + 3, 2, 2, window, 220);
        }
      }
    }
  }

  for (const ServiceFacility& facility : facilities) {
    if (facility.position != coord) continue;
    const RGB color = serviceFacilityColor(facility.type);
    const int margin = std::max(2, size / 6);
    drawFilledRect(renderer, x + margin + 2, y + margin + 2, size - margin * 2, size - margin * 2, {40, 43, 48}, 130);
    drawFilledRect(renderer, x + margin, y + margin, size - margin * 2, size - margin * 2, color);
    if (size >= 12) {
      drawFilledRect(renderer, x + size / 2 - 1, y + margin + 2, 3, size - margin * 2 - 4, {245, 245, 235});
      drawFilledRect(renderer, x + margin + 2, y + size / 2 - 1, size - margin * 2 - 4, 3, {245, 245, 235});
    }
    break;
  }

  if (size >= 10) {
    drawRectOutline(renderer, x, y, size, size, {32, 42, 36}, 30);
  }
}

void drawDiamond(DrawList* renderer, ScreenPoint top, int width, int height, RGB color, uint8_t alpha) {
  if (width <= 0 || height <= 0) return;
  setDrawColor(renderer, color.r, color.g, color.b, alpha);
  renderer->polygon({{top.x, top.y},
                     {top.x + width / 2, top.y + height / 2},
                     {top.x, top.y + height},
                     {top.x - width / 2, top.y + height / 2}});
}

void drawIsometricObject(
  DrawList* renderer,
  ScreenPoint top,
  int tileWidth,
  int tileHeight,
  RGB facade,
  int objectHeight
) {
  if (tileWidth <= 0 || tileHeight <= 0 || objectHeight <= 0) return;
  const int halfWidth = std::max(1, tileWidth / 2 - std::max(2, tileWidth / 7));
  const int halfDepth = std::max(1, tileHeight / 3);
  const int baseY = top.y + tileHeight / 2;
  const int roofY = baseY - objectHeight;
  const auto tint = [](RGB c, int delta) -> RGB {
    return {static_cast<uint8_t>(std::clamp(c.r + delta, 0, 255)),
            static_cast<uint8_t>(std::clamp(c.g + delta, 0, 255)),
            static_cast<uint8_t>(std::clamp(c.b + delta, 0, 255))};
  };
  const auto quad = [&](const std::vector<Point>& points, RGB color, uint8_t alpha = 255) {
    setDrawColor(renderer, color.r, color.g, color.b, alpha);
    renderer->polygon(points);
  };
  // Grounded shadow, then the two visible faces of a single projected prism.
  const int shadow = std::max(2, objectHeight / 4);
  quad({{top.x - halfWidth, baseY}, {top.x, baseY - halfDepth},
        {top.x + halfWidth + shadow, baseY + shadow / 2},
        {top.x + shadow, baseY + halfDepth + shadow / 2}}, {30, 43, 39}, 65);
  quad({{top.x - halfWidth, roofY}, {top.x, roofY + halfDepth},
        {top.x, baseY + halfDepth}, {top.x - halfWidth, baseY}}, facade);
  quad({{top.x, roofY + halfDepth}, {top.x + halfWidth, roofY},
        {top.x + halfWidth, baseY}, {top.x, baseY + halfDepth}}, tint(facade, -40));
  drawDiamond(renderer, {top.x, roofY - halfDepth}, halfWidth * 2,
              halfDepth * 2, tint(facade, 30));

  if (tileWidth >= 22 && objectHeight >= 8) {
    // Window quads follow each wall's slope, with floor spacing tied to zoom.
    const int floorStep = std::max(5, tileHeight / 3);
    const int columns = std::clamp(halfWidth / 6, 1, 5);
    const int windowWidth = std::max(2, halfWidth / (columns * 2 + 1));
    for (int floor = 4; floor + 3 < objectHeight; floor += floorStep) {
      for (int column = 0; column < columns; ++column) {
        const int offset = (column + 1) * halfWidth / (columns + 1);
        const int rise = offset * halfDepth / halfWidth;
        const int slope = std::max(1, windowWidth * halfDepth / halfWidth);
        const int wy = roofY + halfDepth - rise + floor;
        quad({{top.x - offset, wy}, {top.x - offset + windowWidth, wy + slope},
              {top.x - offset + windowWidth, wy + slope + 2}, {top.x - offset, wy + 2}},
             {199, 225, 226});
        quad({{top.x + offset - windowWidth, wy + slope}, {top.x + offset, wy},
              {top.x + offset, wy + 2}, {top.x + offset - windowWidth, wy + slope + 2}},
             {137, 183, 201});
      }
    }
    // A smaller rooftop plant box makes tall silhouettes less uniform.
    if (objectHeight > tileHeight * 2) {
      drawDiamond(renderer, {top.x, roofY - halfDepth / 2},
                  std::max(4, halfWidth / 2), std::max(2, halfDepth / 2), tint(facade, -12));
    }
  }
}

namespace {
void drawIsometricFacility(DrawList* renderer, ScreenPoint top, int width, int height,
                           const ServiceFacility& facility) {
  const RGB color = serviceFacilityColor(facility.type);
  const int cx = top.x;
  const int baseY = top.y + height / 2;
  const int unit = std::max(1, width / 16);
  const auto rect = [&](int x, int y, int w, int h, RGB tint) {
    drawFilledRect(renderer, cx + x * unit, baseY + y * unit,
                   std::max(1, w * unit), std::max(1, h * unit), tint);
  };
  if (facility.type == ServiceType::Cemetery) {
    drawDiamond(renderer, {cx, top.y + height / 8}, width * 3 / 4,
                height * 3 / 4, {89, 126, 83});
    for (int row = 0; row < 2; ++row) {
      for (int column = 0; column < 3; ++column) {
        rect(-4 + column * 3, -2 + row * 3, 2, 3, {146, 155, 149});
        rect(-4 + column * 3, -2 + row * 3, 2, 1, {217, 219, 204});
      }
    }
    return;
  }
  if (facility.type == ServiceType::Water) {
    // Raised reservoir: legs, narrow tank, and a bright cap.
    rect(-3, -7, 1, 8, {108, 120, 129});
    rect(2, -7, 1, 8, {87, 100, 113});
    drawIsometricObject(renderer, {cx, top.y - height / 2}, width * 3 / 5,
                        std::max(4, height / 2), color, std::max(8, height / 2));
    return;
  }
  const int lift = facility.type == ServiceType::Health ? height * 3 / 2 : height;
  drawIsometricObject(renderer, top, width, height, color, lift);
  const int roofY = baseY - lift;
  if (width < 22) return;
  const RGB cream{235, 235, 217};
  switch (facility.type) {
    case ServiceType::Fire:
      // Three garage bays with red lintels.
      for (int bay = 0; bay < 3; ++bay) {
        rect(-5 + bay * 3, -2, 2, 3, {62, 70, 74});
        rect(-5 + bay * 3, -3, 2, 1, cream);
      }
      break;
    case ServiceType::Police:
      setDrawColor(renderer, 209, 221, 231, 255);
      drawLine(renderer, cx, roofY - height / 2, cx, roofY);
      drawFilledRect(renderer, cx, roofY - height / 2, 3 * unit, unit, {81, 140, 211});
      break;
    case ServiceType::Health:
      drawFilledRect(renderer, cx - unit, roofY - 3 * unit, 2 * unit, 6 * unit, cream);
      drawFilledRect(renderer, cx - 3 * unit, roofY - unit, 6 * unit, 2 * unit, cream);
      break;
    case ServiceType::Education:
      drawIsometricObject(renderer, {cx, top.y - height / 2}, width / 3,
                          std::max(4, height / 3), color, height);
      drawFilledRect(renderer, cx - unit, roofY - height / 3, 2 * unit, 2 * unit, cream);
      break;
    case ServiceType::Power:
    case ServiceType::Crematorium: {
      const int chimney = facility.type == ServiceType::Power ? height : height / 2;
      drawFilledRect(renderer, cx + 2 * unit, roofY - chimney, 2 * unit, chimney, {98, 104, 107});
      drawFilledRect(renderer, cx + unit, roofY - chimney, 4 * unit, unit, cream);
      if (facility.type == ServiceType::Power)
        drawDiamond(renderer, {cx - 2 * unit, roofY - unit}, width / 3,
                    std::max(2, height / 3), {71, 103, 132});
      break;
    }
    case ServiceType::Sanitation:
      drawDiamond(renderer, {cx, roofY - height / 4}, width / 2,
                  std::max(3, height / 2), {91, 148, 159});
      drawDiamond(renderer, {cx, roofY - height / 4 + unit}, width / 3,
                  std::max(2, height / 3), {58, 99, 114});
      break;
    case ServiceType::Garbage:
    case ServiceType::Recycling:
      for (int bin = 0; bin < 3; ++bin) {
        rect(-4 + bin * 3, -1, 2, 3, facility.type == ServiceType::Recycling
            ? RGB{60, 137, 83} : RGB{96, 102, 88});
        rect(-4 + bin * 3, -1, 2, 1, cream);
      }
      break;
    case ServiceType::Water:
    case ServiceType::Cemetery:
      break;
  }
}
} // namespace

void drawIsometricTile(
  DrawList* renderer,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  const std::unordered_map<Coord, float, Vec2Hash>& routeHeatByTile,
  float happinessPenalty,
  OverlayMode overlayMode,
  const IsometricProjection& projection,
  Coord coord,
  const DistrictSystem* districts,
  DistrictId selectedDistrictId,
  const TransitMarks* transitMarks
) {
  const Tile& tile = map.getTile(coord);
  const ScreenPoint top = projection.tileTop(coord);
  const int width = projection.tileWidth();
  const int height = projection.tileHeight();
  RGB base = tileColor(
    map, roads, store, coord, overlayMode, facilities, routeHeatByTile, happinessPenalty
  );
  const uint32_t variation = visualHash(coord);
  const bool hasFacility = std::any_of(facilities.begin(), facilities.end(),
      [&](const ServiceFacility& facility) { return facility.position == coord; });
  if (overlayMode == OverlayMode::Zone && hasFacility) base = {177, 184, 173};
  if (overlayMode == OverlayMode::Zone && !tile.hasRoad && tile.buildingId == 0) {
    const int tone = static_cast<int>(variation % 11u) - 5;
    base.r = static_cast<uint8_t>(std::clamp(base.r + tone, 0, 255));
    base.g = static_cast<uint8_t>(std::clamp(base.g + tone, 0, 255));
    base.b = static_cast<uint8_t>(std::clamp(base.b + tone, 0, 255));
  }
  drawDiamond(renderer, top, width, height, base);
  setDrawColor(renderer, 55, 70, 62, overlayMode == OverlayMode::Zone ? 25 : 80);
  const Point outline[5] = {
    {top.x, top.y}, {top.x + width / 2, top.y + height / 2},
    {top.x, top.y + height}, {top.x - width / 2, top.y + height / 2}, {top.x, top.y}
  };
  drawLines(renderer, outline, 5);

  if (overlayMode != OverlayMode::Zone) return;
  const ScreenPoint center = projection.tileCenter(coord);
  if (tile.type == 2 && width >= 16) {
    const uint32_t phase = (renderer->frameTimeMs / 220u + visualHash(coord)) % 5u;
    const int rippleY = top.y + height / 3 + static_cast<int>(phase);
    setDrawColor(renderer, 161, 207, 233, 95);
    drawLine(renderer, center.x - width / 6, rippleY,
                       center.x + width / 6, rippleY);
  }
  // Parcel-local coordinates keep ground details inside their own tile.
  const auto groundPoint = [&](int u, int v) -> Point {
    return {top.x + (u - v) * width / 200,
            top.y + (u + v) * height / 200};
  };
  const auto groundRect = [&](int u, int v, int w, int h, RGB color, uint8_t alpha = 255) {
    setDrawColor(renderer, color.r, color.g, color.b, alpha);
    renderer->polygon({groundPoint(u, v), groundPoint(u + w, v),
                       groundPoint(u + w, v + h), groundPoint(u, v + h)});
  };
  const std::array<Coord, 4> neighbors = {{{coord.x + 1, coord.y}, {coord.x - 1, coord.y},
                                        {coord.x, coord.y + 1}, {coord.x, coord.y - 1}}};
  if (tile.type != 2) {
    for (size_t i = 0; i < neighbors.size(); ++i) {
      if (!map.isValid(neighbors[i]) || map.getTile(neighbors[i]).type != 2) continue;
      const int u = i == 0 ? 87 : 0;
      const int v = i == 2 ? 87 : 0;
      groundRect(u, v, i < 2 ? 13 : 100, i < 2 ? 100 : 13, {218, 207, 150}, 220);
    }
  }
  if (tile.hasRoad) {
    std::array<bool, 4> connected{};
    int connections = 0;
    bool arterial = false;
    for (size_t i = 0; i < neighbors.size(); ++i) {
      connected[i] = roads.hasRoad(coord, neighbors[i]);
      connections += connected[i] ? 1 : 0;
      if (connected[i])
        arterial |= roads.getRoadClass(coord, neighbors[i]) == RoadClass::Arterial;
    }
    const int low = arterial ? 16 : 24;
    const int high = 100 - low;
    const RGB asphalt{57, 62, 67};
    groundRect(low, low, high - low, high - low, asphalt);
    for (size_t i = 0; i < connected.size(); ++i) {
      if (!connected[i]) continue;
      const int u = i == 0 ? high : (i == 1 ? 0 : low);
      const int v = i == 2 ? high : (i == 3 ? 0 : low);
      groundRect(u, v, i < 2 ? low : high - low, i < 2 ? high - low : low, asphalt);
      if (width < 24) continue;
      // Lane markings stop before junctions; arterial roads get double lines.
      const RGB marking{225, 199, 113};
      for (int offset : {arterial ? 47 : 49, arterial ? 53 : 49}) {
        if (i < 2) groundRect(i == 0 ? 76 : 5, offset, 19, 3, marking, 220);
        else groundRect(offset, i == 2 ? 76 : 5, 3, 19, marking, 220);
        if (!arterial) break;
      }
      if (connections >= 3 && width >= 32) {
        for (int stripe = low + 4; stripe < high - 3; stripe += 10) {
          if (i < 2) groundRect(i == 0 ? high - 6 : low + 2, stripe, 4, 6, {223, 227, 221}, 220);
          else groundRect(stripe, i == 2 ? high - 6 : low + 2, 6, 4, {223, 227, 221}, 220);
        }
      }
    }
  }

  if (ParkSystem::isPark(map, coord)) {
    groundRect(6, 6, 88, 88, {77, 155, 87});
    groundRect(44, 0, 12, 100, {211, 198, 156});
    groundRect(0, 44, 100, 12, {211, 198, 156});
    groundRect(13, 62, 22, 7, {136, 91, 54});
    groundRect(13, 62, 22, 2, {187, 134, 74});
    for (int offset : {24, 76}) {
      const Point tree = groundPoint(offset, offset);
      const int crown = std::max(3, width / 8);
      const int lift = std::max(4, height / 2);
      drawDiamond(renderer, {tree.x + 2, tree.y - 1}, crown * 2, std::max(2, height / 4), {31, 76, 45}, 100);
      drawFilledRect(renderer, tree.x - 1, tree.y - lift, 2, lift, {101, 76, 45});
      setDrawColor(renderer, 45, 111, 61, 255);
      renderer->polygon({{tree.x, tree.y - lift - crown * 2},
                         {tree.x + crown, tree.y - lift - crown},
                         {tree.x + crown, tree.y - lift},
                         {tree.x, tree.y - lift + crown / 2},
                         {tree.x - crown, tree.y - lift},
                         {tree.x - crown, tree.y - lift - crown}});
      setDrawColor(renderer, 93, 181, 92, 255);
      renderer->polygon({{tree.x, tree.y - lift - crown * 2},
                         {tree.x, tree.y - lift},
                         {tree.x - crown, tree.y - lift},
                         {tree.x - crown, tree.y - lift - crown}});
    }
  }

  if (transitMarks && tile.hasRoad) {
    drawTransitTile(renderer, *transitMarks, coord,
                    {top.x, top.y + height / 2}, width, height, true);
  }

  // District boundaries belong to the ground layer, before walls and roofs.
  if (districts) {
    for (const District& district : districts->getDistricts()) {
      if (coord.x < district.minCorner.x || coord.x > district.maxCorner.x ||
          coord.y < district.minCorner.y || coord.y > district.maxCorner.y) continue;
      const RGB tint = district.id == selectedDistrictId ? RGB{255, 245, 150}
                                                        : districtArchetypeColor(district.archetype);
      setDrawColor(renderer, tint.r, tint.g, tint.b, district.id == selectedDistrictId ? 230 : 160);
      const auto edge = [&](int u, int v, int endU, int endV) {
        const Point a = groundPoint(u, v), b = groundPoint(endU, endV);
        drawLine(renderer, a.x, a.y, b.x, b.y);
      };
      if (coord.x == district.minCorner.x) edge(0, 0, 0, 100);
      if (coord.x == district.maxCorner.x) edge(100, 0, 100, 100);
      if (coord.y == district.minCorner.y) edge(0, 0, 100, 0);
      if (coord.y == district.maxCorner.y) edge(0, 100, 100, 100);
    }
  }

  if (tile.buildingId != 0) {
    const Building* building = store.getBuilding(tile.buildingId);
    if (building != nullptr) {
      const float occupancy = building->capacity > 0
        ? std::clamp(static_cast<float>(building->occupancy) / static_cast<float>(building->capacity), 0.0f, 1.0f)
        : 0.0f;
      const int tier = buildingDensityTier(building->type, building->capacity);
      const int typeFloors = building->type == BuildingType::Office
        ? 2 : (building->type == BuildingType::Commercial ? 1 : 0);
      const int objectHeight = std::max(10, height + (typeFloors + tier + 1) * height / 2);
      RGB facade = buildingFacade(building->type);
      const int tone = static_cast<int>(visualHash(coord) % 19u) - 9;
      facade.r = static_cast<uint8_t>(std::clamp(facade.r + tone, 0, 255));
      facade.g = static_cast<uint8_t>(std::clamp(facade.g + tone, 0, 255));
      facade.b = static_cast<uint8_t>(std::clamp(facade.b + tone, 0, 255));
      groundRect(44, 72, 12, 28, {182, 183, 167});
      drawIsometricObject(renderer, top, width, height, facade, objectHeight);
      if (width >= 24) {
        const int doorWidth = std::max(2, width / 12);
        const int doorHeight = std::max(4, height / 3);
        setDrawColor(renderer, 60, 80, 83, 255);
        renderer->polygon({{center.x - doorWidth, center.y + height / 3 - doorHeight},
                           {center.x, center.y + height / 3 - doorHeight + doorWidth / 2},
                           {center.x, center.y + height / 3},
                           {center.x - doorWidth, center.y + height / 3 - doorWidth / 2}});
      }
      const int baseY = center.y;
      const int roofY = baseY - objectHeight;
      switch (building->type) {
        case BuildingType::Residential: {
          const int roofHalf = std::max(1, width / 2 - std::max(2, width / 7));
          const int depth = std::max(1, height / 3);
          const int ridge = std::max(3, height / 3);
          setDrawColor(renderer, 151, 91, 66, 255);
          renderer->polygon({{center.x - roofHalf, roofY},
                             {center.x, roofY - depth - ridge},
                             {center.x, roofY + depth - ridge},
                             {center.x, roofY + depth}});
          setDrawColor(renderer, 111, 66, 53, 255);
          renderer->polygon({{center.x, roofY - depth - ridge},
                             {center.x + roofHalf, roofY},
                             {center.x, roofY + depth},
                             {center.x, roofY + depth - ridge}});
          setDrawColor(renderer, 176, 111, 79, 255);
          drawLine(renderer, center.x, roofY - depth - ridge,
                            center.x, roofY + depth - ridge);
          break;
        }
        case BuildingType::Commercial:
          setDrawColor(renderer, 235, 185, 65, 255);
          renderer->polygon({{center.x - width / 3, baseY - height / 4},
                             {center.x, baseY + height / 12},
                             {center.x + width / 12, baseY},
                             {center.x - width / 4, baseY - height / 3}});
          break;
        case BuildingType::Industrial:
          drawFilledRect(renderer, center.x + width / 6, roofY - 7,
                         std::max(2, width / 8), 8, {91, 86, 78});
          drawFilledRect(renderer, center.x + width / 6 - 1, roofY - 8,
                         std::max(3, width / 8 + 2), 2, {145, 145, 135});
          break;
        case BuildingType::Office:
          setDrawColor(renderer, 190, 210, 220, 230);
          drawLine(renderer, center.x, roofY - height / 3 - 5,
                            center.x, roofY - height / 3 + 2);
          break;
      }
      if (occupancy < 0.15f) {
        setDrawColor(renderer, 232, 172, 65, 230);
        drawLine(renderer, center.x - width / 3, baseY - 2,
                          center.x - width / 3, baseY - height);
        drawLine(renderer, center.x + width / 3, baseY - 2,
                          center.x + width / 3, baseY - height);
        drawLine(renderer, center.x - width / 3, baseY - height / 2,
                          center.x + width / 3, baseY - height / 2);
      }
    }
  }
  for (const ServiceFacility& facility : facilities) {
    if (facility.position == coord) {
      drawIsometricFacility(renderer, top, width, height, facility);
      break;
    }
  }

  if (!tile.hasRoad && tile.buildingId == 0 && !hasFacility && tile.zone == 0 && !tile.regionalConnectionId && tile.type == 0
      && variation % 11u == 0u && width >= 14) {
    const int trunkHeight = std::max(3, height / 2);
    const int crownWidth = std::max(4, width / 5);
    const int crownHeight = std::max(5, height * 3 / 4);
    drawDiamond(renderer, {center.x + width / 12, center.y - height / 8},
                crownWidth * 2, std::max(3, height / 3), {38, 65, 40}, 65);
    drawFilledRect(renderer, center.x - 1, center.y - trunkHeight, 2, trunkHeight,
                   {101, 76, 45});
    const int crownY = center.y - trunkHeight - crownHeight / 2;
    setDrawColor(renderer, 48, 112, 61, 255);
    renderer->polygon({{center.x, crownY - crownHeight},
                       {center.x + crownWidth, crownY - crownHeight / 2},
                       {center.x + crownWidth, crownY},
                       {center.x, crownY + crownHeight / 3},
                       {center.x - crownWidth, crownY},
                       {center.x - crownWidth, crownY - crownHeight / 2}});
    setDrawColor(renderer, 77, static_cast<uint8_t>(145 + variation % 17u), 77, 255);
    renderer->polygon({{center.x, crownY - crownHeight},
                       {center.x, crownY}, {center.x - crownWidth, crownY},
                       {center.x - crownWidth, crownY - crownHeight / 2}});
  }

}

void drawGradientBar(DrawList* renderer, int x, int y, int w, int h, OverlayMode mode) {
  for (int i = 0; i < w; ++i) {
    const float t = (w > 1) ? static_cast<float>(i) / static_cast<float>(w - 1) : 0.0f;
    const RGB c = overlaySampleColor(mode, t);
    drawFilledRect(renderer, x + i, y, 1, h, c, 255);
  }
  drawRectOutline(renderer, x, y, w, h, {220, 220, 220}, 255);
}

std::array<uint8_t, 5> glyphRows(char c) {
  switch (c) {
    case 'A': return {0b010, 0b101, 0b111, 0b101, 0b101};
    case 'B': return {0b110, 0b101, 0b110, 0b101, 0b110};
    case 'C': return {0b011, 0b100, 0b100, 0b100, 0b011};
    case 'D': return {0b110, 0b101, 0b101, 0b101, 0b110};
    case 'E': return {0b111, 0b100, 0b110, 0b100, 0b111};
    case 'F': return {0b111, 0b100, 0b110, 0b100, 0b100};
    case 'G': return {0b011, 0b100, 0b101, 0b101, 0b011};
    case 'H': return {0b101, 0b101, 0b111, 0b101, 0b101};
    case 'I': return {0b111, 0b010, 0b010, 0b010, 0b111};
    case 'J': return {0b001, 0b001, 0b001, 0b101, 0b010};
    case 'K': return {0b101, 0b101, 0b110, 0b101, 0b101};
    case 'L': return {0b100, 0b100, 0b100, 0b100, 0b111};
    case 'M': return {0b101, 0b111, 0b111, 0b101, 0b101};
    case 'N': return {0b101, 0b111, 0b111, 0b111, 0b101};
    case 'O': return {0b010, 0b101, 0b101, 0b101, 0b010};
    case 'P': return {0b110, 0b101, 0b110, 0b100, 0b100};
    case 'Q': return {0b010, 0b101, 0b101, 0b111, 0b011};
    case 'R': return {0b110, 0b101, 0b110, 0b101, 0b101};
    case 'S': return {0b011, 0b100, 0b010, 0b001, 0b110};
    case 'T': return {0b111, 0b010, 0b010, 0b010, 0b010};
    case 'U': return {0b101, 0b101, 0b101, 0b101, 0b111};
    case 'V': return {0b101, 0b101, 0b101, 0b101, 0b010};
    case 'W': return {0b101, 0b101, 0b111, 0b111, 0b101};
    case 'X': return {0b101, 0b101, 0b010, 0b101, 0b101};
    case 'Y': return {0b101, 0b101, 0b010, 0b010, 0b010};
    case 'Z': return {0b111, 0b001, 0b010, 0b100, 0b111};
    case '0': return {0b111, 0b101, 0b101, 0b101, 0b111};
    case '1': return {0b010, 0b110, 0b010, 0b010, 0b111};
    case '2': return {0b110, 0b001, 0b111, 0b100, 0b111};
    case '3': return {0b110, 0b001, 0b111, 0b001, 0b110};
    case '4': return {0b101, 0b101, 0b111, 0b001, 0b001};
    case '5': return {0b111, 0b100, 0b111, 0b001, 0b110};
    case '6': return {0b011, 0b100, 0b111, 0b101, 0b111};
    case '7': return {0b111, 0b001, 0b001, 0b001, 0b001};
    case '8': return {0b111, 0b101, 0b111, 0b101, 0b111};
    case '9': return {0b111, 0b101, 0b111, 0b001, 0b110};
    case '+': return {0b000, 0b010, 0b111, 0b010, 0b000};
    case '%': return {0b101, 0b001, 0b010, 0b100, 0b101};
    case '-': return {0b000, 0b000, 0b111, 0b000, 0b000};
    case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
    case '$': return {0b010, 0b111, 0b110, 0b011, 0b010};
    case '/': return {0b001, 0b001, 0b010, 0b100, 0b100};
    default: return {0b000, 0b000, 0b000, 0b000, 0b000};
  }
}

void drawGlyph(DrawList* renderer, int x, int y, char raw, RGB color, int scale) {
  char c = raw;
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }

  const std::array<uint8_t, 5> rows = glyphRows(c);
  for (int ry = 0; ry < 5; ++ry) {
    for (int rx = 0; rx < 3; ++rx) {
      const bool on = (rows[ry] & (1 << (2 - rx))) != 0;
      if (!on) {
        continue;
      }
      drawFilledRect(renderer, x + rx * scale, y + ry * scale, scale, scale, color, 255);
    }
  }
}

void drawText(DrawList* renderer, int x, int y, const std::string& text, RGB color, int scale) {
  // The original 1x 3-by-5 glyphs are only five physical pixels tall and are
  // effectively unreadable on Retina/high-density displays. Promote only
  // compact labels to 2x by default; headings already passed 2x-4x explicitly
  // and must retain their layout dimensions. F2 toggles the legacy compact
  // mode for unusually small windows.
  const int effectiveScale = (gReadableUiText && scale == 1) ? 2 : scale;
  int cursorX = x;
  for (char c : text) {
    if (c == ' ') {
      cursorX += effectiveScale * 2;
      continue;
    }
    drawGlyph(renderer, cursorX, y, c, color, effectiveScale);
    cursorX += (3 * effectiveScale) + effectiveScale;
  }
}

constexpr int kPaletteButtonWidth = 112;
constexpr int kPaletteButtonHeight = 42;
constexpr int kPaletteGap = 8;
constexpr int kPaletteBottomMargin = 14;

int paletteWidth() {
  return (5 * kPaletteButtonWidth) + (4 * kPaletteGap);
}

Rect paletteButtonRect(int index, int windowWidth, int windowHeight) {
  const int startX = (windowWidth - paletteWidth()) / 2;
  const int y = windowHeight - kPaletteBottomMargin - kPaletteButtonHeight;
  return {startX + index * (kPaletteButtonWidth + kPaletteGap), y,
          kPaletteButtonWidth, kPaletteButtonHeight};
}

PaletteTool paletteHitTest(int mouseX, int mouseY, int windowWidth, int windowHeight) {
  const std::array<PaletteTool, 5> tools = {
    PaletteTool::Road, PaletteTool::Zone, PaletteTool::Bulldoze, PaletteTool::Service,
    PaletteTool::District
  };
  for (size_t i = 0; i < tools.size(); ++i) {
    const Rect rect = paletteButtonRect(static_cast<int>(i), windowWidth, windowHeight);
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      return tools[i];
    }
  }
  return PaletteTool::None;
}

void drawDistrictOutlines(
  DrawList* renderer,
  const DistrictSystem& districts,
  bool isometricMode,
  const IsometricProjection& projection,
  int viewX,
  int viewY,
  int tilePixels,
  DistrictId selectedId
) {
  for (const District& district : districts.getDistricts()) {
    const bool selected = district.id == selectedId;
    const RGB color = selected ? RGB{255, 245, 150} : districtArchetypeColor(district.archetype);
    const uint8_t isoAlpha = selected ? 170 : 90;
    const uint8_t topAlpha = selected ? 255 : 200;
    auto drawTile = [&](int x, int y) {
      const Coord coord{x, y};
      if (isometricMode) {
        drawDiamond(renderer, projection.tileTop(coord), projection.tileWidth(),
                    projection.tileHeight(), color, isoAlpha);
      } else {
        const int screenX = (x - viewX) * tilePixels;
        const int screenY = (y - viewY) * tilePixels;
        drawRectOutline(renderer, screenX, screenY, tilePixels, tilePixels, color, topAlpha);
      }
    };
    for (int x = district.minCorner.x; x <= district.maxCorner.x; ++x) {
      drawTile(x, district.minCorner.y);
      if (district.maxCorner.y != district.minCorner.y) {
        drawTile(x, district.maxCorner.y);
      }
    }
    for (int y = district.minCorner.y + 1; y <= district.maxCorner.y - 1; ++y) {
      drawTile(district.minCorner.x, y);
      if (district.maxCorner.x != district.minCorner.x) {
        drawTile(district.maxCorner.x, y);
      }
    }
  }
}

}  // namespace visualizer

namespace visualizer {
void drawPortTile(DrawList* renderer,ScreenPoint top,int width,int height,bool isometric,
                  regional::Kind kind,bool enabled,bool first) {
  const bool cargo=kind==regional::Kind::CargoPort;
  const RGB yard=enabled ? RGB{157,171,174} : RGB{113,123,126};
  const RGB facade=enabled ? (cargo ? (first ? RGB{205,128,68} : RGB{56,125,157}) : RGB{203,220,223})
                           : RGB{113,123,131};
  if (isometric) {
    drawDiamond(renderer,top,width,height,yard);
    const int w=std::max(4,width*2/3), h=std::max(2,height*2/3);
    const ScreenPoint roof{top.x,top.y+height/5};
    drawIsometricObject(renderer,roof,w,h,facade,std::max(3,height*(cargo ? 2 : 3)/5));
    if (!cargo) {
      const int halfWidth=std::max(1,w/2-std::max(2,w/7)), halfDepth=std::max(1,h/3);
      drawDiamond(renderer,{roof.x,roof.y+h/2-std::max(3,height*3/5)-halfDepth},
        halfWidth*2,halfDepth*2,{56,123,160});
    }
  } else {
    drawFilledRect(renderer,top.x,top.y,width,height,yard);
    const int pad=std::max(2,width/6);
    drawFilledRect(renderer,top.x+pad,top.y+pad,width-2*pad,height-2*pad,facade);
    const RGB detail=cargo ? RGB{235,209,154} : RGB{56,123,160};
    drawFilledRect(renderer,top.x+pad,top.y+height/2,width-2*pad,std::max(2,height/8),detail);
    drawRectOutline(renderer,top.x+pad,top.y+pad,width-2*pad,height-2*pad,{65,83,96});
  }
}
} // namespace visualizer
