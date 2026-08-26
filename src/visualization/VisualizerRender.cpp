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

#include <SDL.h>

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

#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/VisualizerOverlay.hpp"


namespace visualizer {

void drawFilledRect(SDL_Renderer* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
  SDL_Rect rect{x, y, w, h};
  SDL_RenderFillRect(renderer, &rect);
}

void drawRectOutline(SDL_Renderer* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
  SDL_Rect rect{x, y, w, h};
  SDL_RenderDrawRect(renderer, &rect);
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

void drawNormalTileDetail(
  SDL_Renderer* renderer,
  const CityMap& map,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  Coord coord,
  int x,
  int y,
  int size
) {
  const Tile& tile = map.getTile(coord);
  const uint32_t variation = visualHash(coord);

  if (tile.type == 2) {
    if (size >= 6) {
      const int bandY = y + 2 + static_cast<int>((variation + SDL_GetTicks() / 180u) % std::max(2, size - 3));
      drawFilledRect(renderer, x + 1, bandY, std::max(1, size - 2), 1, {145, 190, 250}, 120);
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
    if (tile.zone == 0 && tile.type == 0 && size >= 14 && variation % 11u == 0u) {
      const int crown = std::max(3, size / 4);
      const int treeX = x + 2 + static_cast<int>((variation >> 12) % static_cast<uint32_t>(std::max(1, size - crown - 3)));
      const int treeY = y + 2 + static_cast<int>((variation >> 20) % static_cast<uint32_t>(std::max(1, size - crown - 4)));
      drawFilledRect(renderer, treeX + crown / 2, treeY + crown - 1, 2, 3, {105, 78, 46});
      drawFilledRect(renderer, treeX, treeY + 1, crown, crown, {54, 123, 66});
      drawFilledRect(renderer, treeX + 1, treeY, std::max(2, crown - 1), crown, {72, 151, 79});
    }
  }

  if (tile.hasRoad) {
    const int roadWidth = std::max(4, size * 3 / 5);
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
      const float occupancy = building->capacity > 0
        ? std::clamp(static_cast<float>(building->occupancy) / static_cast<float>(building->capacity), 0.0f, 1.0f)
        : 0.0f;
      const int margin = std::max(2, size / 7);
      const int lift = size >= 18 ? 2 + static_cast<int>(occupancy * 3.0f) : 1;
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

void drawDiamond(SDL_Renderer* renderer, ScreenPoint top, int width, int height, RGB color, uint8_t alpha) {
  const int halfWidth = width / 2;
  const int halfHeight = std::max(1, height / 2);
  for (int row = 0; row < height; ++row) {
    const int distance = std::abs(row - halfHeight);
    const int rowHalfWidth = std::max(1, halfWidth - (distance * halfWidth) / halfHeight);
    drawFilledRect(renderer, top.x - rowHalfWidth, top.y + row, rowHalfWidth * 2, 1, color, alpha);
  }
}

void drawIsometricObject(
  SDL_Renderer* renderer,
  ScreenPoint top,
  int tileWidth,
  int tileHeight,
  RGB facade,
  int objectHeight
) {
  const int halfWidth = tileWidth / 2;
  const int inset = std::max(2, tileWidth / 7);
  const int left = top.x - halfWidth + inset;
  const int width = tileWidth - inset * 2;
  const int baseY = top.y + tileHeight / 2;
  drawFilledRect(renderer, left + 2, baseY - objectHeight + 3, width, objectHeight, {35, 40, 40}, 95);
  drawFilledRect(renderer, left, baseY - objectHeight, width / 2, objectHeight, facade);
  const RGB shaded{
    static_cast<uint8_t>(facade.r * 3 / 4),
    static_cast<uint8_t>(facade.g * 3 / 4),
    static_cast<uint8_t>(facade.b * 3 / 4)
  };
  drawFilledRect(renderer, left + width / 2, baseY - objectHeight, width - width / 2, objectHeight, shaded);
  drawDiamond(renderer, {top.x, baseY - objectHeight - tileHeight / 3}, width, std::max(4, tileHeight * 2 / 3), {
    static_cast<uint8_t>(std::min(255, facade.r + 30)),
    static_cast<uint8_t>(std::min(255, facade.g + 30)),
    static_cast<uint8_t>(std::min(255, facade.b + 30))
  });
  if (tileWidth >= 22 && objectHeight >= 8) {
    drawFilledRect(renderer, left + 3, baseY - objectHeight + 3, 2, 2, {205, 230, 225});
    drawFilledRect(renderer, left + width - 5, baseY - objectHeight + 3, 2, 2, {170, 208, 220});
  }
}

void drawIsometricTile(
  SDL_Renderer* renderer,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  const std::unordered_map<Coord, float, Vec2Hash>& routeHeatByTile,
  float happinessPenalty,
  OverlayMode overlayMode,
  const IsometricProjection& projection,
  Coord coord
) {
  const Tile& tile = map.getTile(coord);
  const ScreenPoint top = projection.tileTop(coord);
  const int width = projection.tileWidth();
  const int height = projection.tileHeight();
  const RGB base = tileColor(
    map, roads, store, coord, overlayMode, facilities, routeHeatByTile, happinessPenalty
  );
  drawDiamond(renderer, top, width, height, base);
  SDL_SetRenderDrawColor(renderer, 55, 70, 62, 80);
  const SDL_Point outline[5] = {
    {top.x, top.y}, {top.x + width / 2, top.y + height / 2},
    {top.x, top.y + height}, {top.x - width / 2, top.y + height / 2}, {top.x, top.y}
  };
  SDL_RenderDrawLines(renderer, outline, 5);

  if (overlayMode != OverlayMode::Zone) return;
  const ScreenPoint center = projection.tileCenter(coord);
  if (tile.hasRoad) {
    const RGB asphalt{58, 61, 65};
    const int thickness = std::max(2, width / 6);
    drawDiamond(renderer, {center.x, center.y - height / 3}, width * 2 / 3,
                std::max(4, height * 2 / 3), asphalt);
    const std::array<Coord, 4> neighbors = {{{coord.x + 1, coord.y}, {coord.x - 1, coord.y}, {coord.x, coord.y + 1}, {coord.x, coord.y - 1}}};
    for (const Coord neighbor : neighbors) {
      if (!hasRoadAt(map, neighbor)) continue;
      const ScreenPoint target = projection.tileCenter(neighbor);
      SDL_SetRenderDrawColor(renderer, asphalt.r, asphalt.g, asphalt.b, 255);
      const int slope = ((target.x - center.x) * (target.y - center.y) >= 0) ? 1 : -1;
      for (int offset = -thickness / 2; offset <= thickness / 2; ++offset) {
        SDL_RenderDrawLine(renderer, center.x + offset, center.y - slope * offset,
                          target.x + offset, target.y - slope * offset);
      }
    }
  }

  if (tile.buildingId != 0) {
    const Building* building = store.getBuilding(tile.buildingId);
    if (building != nullptr) {
      const float occupancy = building->capacity > 0
        ? std::clamp(static_cast<float>(building->occupancy) / static_cast<float>(building->capacity), 0.0f, 1.0f)
        : 0.0f;
      const int typeFloors = building->type == BuildingType::Office
        ? 2 : (building->type == BuildingType::Commercial ? 1 : 0);
      drawIsometricObject(renderer, top, width, height, buildingFacade(building->type),
                          std::max(10, height + typeFloors * height / 2
                            + static_cast<int>(occupancy * height * 1.5f)));
      const ScreenPoint center = projection.tileCenter(coord);
      const int baseY = center.y;
      switch (building->type) {
        case BuildingType::Residential:
          SDL_SetRenderDrawColor(renderer, 116, 72, 55, 255);
          SDL_RenderDrawLine(renderer, center.x - width / 4, baseY - height - 2,
                            center.x, baseY - height - 7);
          SDL_RenderDrawLine(renderer, center.x, baseY - height - 7,
                            center.x + width / 4, baseY - height - 2);
          break;
        case BuildingType::Commercial:
          drawFilledRect(renderer, center.x - width / 3, baseY - 4,
                         width * 2 / 3, 3, {235, 185, 65});
          break;
        case BuildingType::Industrial:
          drawFilledRect(renderer, center.x + width / 6, baseY - height - 7,
                         std::max(2, width / 8), 8, {91, 86, 78});
          drawFilledRect(renderer, center.x + width / 6 - 1, baseY - height - 8,
                         std::max(3, width / 8 + 2), 2, {145, 145, 135});
          break;
        case BuildingType::Office:
          SDL_SetRenderDrawColor(renderer, 190, 210, 220, 230);
          SDL_RenderDrawLine(renderer, center.x, baseY - height * 2 - 5,
                            center.x, baseY - height * 2 + 2);
          break;
      }
      if (occupancy < 0.15f) {
        SDL_SetRenderDrawColor(renderer, 232, 172, 65, 230);
        SDL_RenderDrawLine(renderer, center.x - width / 3, baseY - 2,
                          center.x - width / 3, baseY - height);
        SDL_RenderDrawLine(renderer, center.x + width / 3, baseY - 2,
                          center.x + width / 3, baseY - height);
        SDL_RenderDrawLine(renderer, center.x - width / 3, baseY - height / 2,
                          center.x + width / 3, baseY - height / 2);
      }
    }
  }
  for (const ServiceFacility& facility : facilities) {
    if (facility.position == coord) {
      drawIsometricObject(renderer, top, width, height, serviceFacilityColor(facility.type), height);
      break;
    }
  }

  const uint32_t variation = visualHash(coord);
  if (!tile.hasRoad && tile.buildingId == 0 && tile.zone == 0 && tile.type != 2
      && variation % 17u == 0u) {
    const int trunkHeight = std::max(3, height / 2);
    drawFilledRect(renderer, center.x - 1, center.y - trunkHeight, 2, trunkHeight,
                   {101, 76, 45});
    drawDiamond(renderer, {center.x, center.y - trunkHeight - height},
                std::max(6, width / 2), std::max(5, height), {48, 121, 62});
    drawDiamond(renderer, {center.x - 1, center.y - trunkHeight - height - 2},
                std::max(5, width * 2 / 5), std::max(4, height * 3 / 4), {70, 153, 76});
  }

  if (tile.type != 2) {
    const RGB shore{218, 207, 150};
    const std::array<Coord, 4> neighbors = {{{coord.x + 1, coord.y}, {coord.x - 1, coord.y}, {coord.x, coord.y + 1}, {coord.x, coord.y - 1}}};
    for (const Coord neighbor : neighbors) {
      if (!map.isValid(neighbor) || map.getTile(neighbor).type != 2) continue;
      const ScreenPoint edge = projection.tileCenter(neighbor);
      SDL_SetRenderDrawColor(renderer, shore.r, shore.g, shore.b, 210);
      SDL_RenderDrawLine(renderer, center.x, center.y, (center.x + edge.x) / 2, (center.y + edge.y) / 2);
    }
  }
}

void drawGradientBar(SDL_Renderer* renderer, int x, int y, int w, int h, OverlayMode mode) {
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
    case '-': return {0b000, 0b000, 0b111, 0b000, 0b000};
    case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
    case '$': return {0b010, 0b111, 0b110, 0b011, 0b010};
    case '/': return {0b001, 0b001, 0b010, 0b100, 0b100};
    default: return {0b000, 0b000, 0b000, 0b000, 0b000};
  }
}

void drawGlyph(SDL_Renderer* renderer, int x, int y, char raw, RGB color, int scale) {
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

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text, RGB color, int scale) {
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
  return (4 * kPaletteButtonWidth) + (3 * kPaletteGap);
}

SDL_Rect paletteButtonRect(int index, int windowWidth, int windowHeight) {
  const int startX = (windowWidth - paletteWidth()) / 2;
  const int y = windowHeight - kPaletteBottomMargin - kPaletteButtonHeight;
  return {startX + index * (kPaletteButtonWidth + kPaletteGap), y,
          kPaletteButtonWidth, kPaletteButtonHeight};
}

PaletteTool paletteHitTest(int mouseX, int mouseY, int windowWidth, int windowHeight) {
  const std::array<PaletteTool, 4> tools = {
    PaletteTool::Road, PaletteTool::Zone, PaletteTool::Bulldoze, PaletteTool::Service
  };
  for (size_t i = 0; i < tools.size(); ++i) {
    const SDL_Rect rect = paletteButtonRect(static_cast<int>(i), windowWidth, windowHeight);
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      return tools[i];
    }
  }
  return PaletteTool::None;
}


}  // namespace visualizer
