#pragma once

#include "src/visualization/VisualizerTypes.hpp"
#include "src/visualization/IsometricProjection.hpp"
#include "src/entities/EntityStore.hpp"
#include "src/systems/ServiceSystem.hpp"

namespace visualizer {

void drawFilledRect(SDL_Renderer* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha = 255);
void drawRectOutline(SDL_Renderer* renderer, int x, int y, int w, int h, RGB color, uint8_t alpha = 255);
bool hasRoadAt(const CityMap& map, Coord coord);

void drawNormalTileDetail(
  SDL_Renderer* renderer,
  const CityMap& map,
  const EntityStore& store,
  const std::vector<ServiceFacility>& facilities,
  Coord coord,
  int x,
  int y,
  int size
);

void drawDiamond(SDL_Renderer* renderer, ScreenPoint top, int width, int height, RGB color, uint8_t alpha = 255);

void drawIsometricObject(
  SDL_Renderer* renderer,
  ScreenPoint top,
  int tileWidth,
  int tileHeight,
  RGB facade,
  int objectHeight
);

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
);

void drawGradientBar(SDL_Renderer* renderer, int x, int y, int w, int h, OverlayMode mode);
void drawGlyph(SDL_Renderer* renderer, int x, int y, char raw, RGB color, int scale);
void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text, RGB color, int scale = 2);

int paletteWidth();
SDL_Rect paletteButtonRect(int index, int windowWidth, int windowHeight);
PaletteTool paletteHitTest(int mouseX, int mouseY, int windowWidth, int windowHeight);

}  // namespace visualizer
