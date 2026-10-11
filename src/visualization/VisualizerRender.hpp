#pragma once

#include "src/visualization/VisualizerTypes.hpp"
#include "src/visualization/IsometricProjection.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/systems/DistrictSystem.hpp"
#include "src/systems/ServiceSystem.hpp"

namespace visualizer {

void drawFilledRect(DrawList* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha = 255);
void drawRectOutline(DrawList* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha = 255);
bool hasRoadAt(const CityMap& map, Coord coord);

void drawNormalTileDetail(
  DrawList* renderer,
  const CityMap& map,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  Coord coord,
  int x,
  int y,
  int size
);

void drawDiamond(DrawList* renderer, ScreenPoint top, int width, int height, RGB color, uint8_t alpha = 255);

void drawIsometricObject(
  DrawList* renderer,
  ScreenPoint top,
  int tileWidth,
  int tileHeight,
  RGB facade,
  int objectHeight
);

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
  Coord coord
);

void drawGradientBar(DrawList* renderer, int x, int y, int w, int h, OverlayMode mode);
void drawGlyph(DrawList* renderer, int x, int y, char raw, RGB color, int scale);
void drawText(DrawList* renderer, int x, int y, const std::string& text, RGB color, int scale = 2);

int paletteWidth();
Rect paletteButtonRect(int index, int windowWidth, int windowHeight);
PaletteTool paletteHitTest(int mouseX, int mouseY, int windowWidth, int windowHeight);
void drawDistrictOutlines(
  DrawList* renderer,
  const DistrictSystem& districts,
  bool isometricMode,
  const IsometricProjection& projection,
  int viewX,
  int viewY,
  int tilePixels,
  DistrictId selectedId = 0
);

}  // namespace visualizer
