#include "src/world/CityMap.hpp"
#include <stdexcept>

CityMap::CityMap(glm::ivec2 dims) 
  : dimensions(dims) {
  if (dims.x <= 0 || dims.y <= 0) {
    throw std::invalid_argument("Invalid map dimensions");
  }
  
  // Initialize all tiles
  tiles.resize(dims.x * dims.y);
  pollutions.assign(dims.x * dims.y, 0.0f);
  landValues.assign(dims.x * dims.y, 100.0f);
  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      Coord pos{x, y};
      Tile& tile = getTile(pos);
      tile.position = pos;
      tile.type = 0;        // Empty
      tile.zone = 0;        // No zone
    }
  }
}

size_t CityMap::getIndex(Coord coord) const {
  if (!isValid(coord)) {
    throw std::out_of_range("Coordinate out of bounds");
  }
  return coord.y * dimensions.x + coord.x;
}

Tile& CityMap::getTile(Coord coord) {
  return tiles[getIndex(coord)];
}

const Tile& CityMap::getTile(Coord coord) const {
  return tiles[getIndex(coord)];
}

bool CityMap::isValid(Coord coord) const {
  return coord.x >= 0 && coord.x < dimensions.x &&
         coord.y >= 0 && coord.y < dimensions.y;
}

float& CityMap::pollution(Coord coord) {
  return pollutions[getIndex(coord)];
}

float CityMap::pollution(Coord coord) const {
  return pollutions[getIndex(coord)];
}

float& CityMap::landValue(Coord coord) {
  return landValues[getIndex(coord)];
}

float CityMap::landValue(Coord coord) const {
  return landValues[getIndex(coord)];
}

int CityMap::zone(Coord coord) const {
  return getTile(coord).zone;
}

void CityMap::setZone(Coord coord, int zoneValue) {
  getTile(coord).zone = zoneValue;
}
