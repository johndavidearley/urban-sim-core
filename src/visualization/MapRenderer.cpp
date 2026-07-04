#include "MapRenderer.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace {
struct RGB {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

RGB zoneColor(int zone) {
  switch (zone) {
    case 1: // Residential
      return {119, 221, 119};
    case 2: // Commercial
      return {100, 149, 237};
    case 3: // Industrial
      return {255, 179, 71};
    case 4: // Park
      return {80, 200, 120};
    case 5: // Office
      return {147, 112, 219};
    default:
      return {230, 230, 230};
  }
}

RGB terrainTint(const Tile& tile, RGB base) {
  if (tile.type == 2) { // water
    return {102, 153, 255};
  }
  if (tile.type == 1) { // terrain
    base.r = static_cast<uint8_t>(std::max(0, base.r - 25));
    base.g = static_cast<uint8_t>(std::max(0, base.g - 25));
    base.b = static_cast<uint8_t>(std::max(0, base.b - 25));
  }
  return base;
}

void setPixel(std::vector<uint8_t>& pixels, int width, int x, int y, RGB color) {
  const size_t idx = static_cast<size_t>((y * width + x) * 3);
  pixels[idx] = color.r;
  pixels[idx + 1] = color.g;
  pixels[idx + 2] = color.b;
}
} // namespace

bool MapRenderer::renderToPPM(
  const std::string& filePath,
  const CityMap& map,
  const EntityStore& store,
  const RenderOptions& options
) {
  const glm::ivec2 dims = map.getDimensions();

  const int tilePixels = std::max(1, options.tilePixels);
  const int viewX = std::clamp(options.viewX, 0, std::max(0, dims.x - 1));
  const int viewY = std::clamp(options.viewY, 0, std::max(0, dims.y - 1));

  const int requestedWidth = (options.viewWidth > 0) ? options.viewWidth : (dims.x - viewX);
  const int requestedHeight = (options.viewHeight > 0) ? options.viewHeight : (dims.y - viewY);

  const int tileWidth = std::max(1, std::min(requestedWidth, dims.x - viewX));
  const int tileHeight = std::max(1, std::min(requestedHeight, dims.y - viewY));

  const int imageWidth = tileWidth * tilePixels;
  const int imageHeight = tileHeight * tilePixels;

  std::vector<uint8_t> pixels(static_cast<size_t>(imageWidth * imageHeight * 3), 0);

  for (int ty = 0; ty < tileHeight; ++ty) {
    for (int tx = 0; tx < tileWidth; ++tx) {
      const Coord coord{viewX + tx, viewY + ty};
      const Tile& tile = map.getTile(coord);

      RGB color = terrainTint(tile, zoneColor(map.zone(coord)));

      if (tile.hasRoad) {
        color = {64, 64, 64};
      }

      if (tile.buildingId != 0) {
        const Building* building = store.getBuilding(tile.buildingId);
        if (building != nullptr) {
          switch (building->type) {
            case BuildingType::Residential:
              color = {44, 132, 70};
              break;
            case BuildingType::Commercial:
              color = {31, 84, 163};
              break;
            case BuildingType::Industrial:
              color = {179, 93, 29};
              break;
            case BuildingType::Office:
              color = {147, 112, 219};
              break;
          }
        }
      }

      const int px0 = tx * tilePixels;
      const int py0 = ty * tilePixels;
      for (int py = 0; py < tilePixels; ++py) {
        for (int px = 0; px < tilePixels; ++px) {
          setPixel(pixels, imageWidth, px0 + px, py0 + py, color);
        }
      }
    }
  }

  std::ofstream out(filePath, std::ios::binary);
  if (!out.is_open()) {
    return false;
  }

  out << "P6\n" << imageWidth << " " << imageHeight << "\n255\n";
  out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
  return static_cast<bool>(out);
}
