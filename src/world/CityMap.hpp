#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "src/world/Tile.hpp"

class CityMap {
private:
  glm::ivec2 dimensions;
  std::vector<Tile> tiles;
  
  size_t getIndex(Coord coord) const;
  
public:
  CityMap(glm::ivec2 dims);
  
  Tile& getTile(Coord coord);
  const Tile& getTile(Coord coord) const;
  
  bool isValid(Coord coord) const;
  
  glm::ivec2 getDimensions() const { return dimensions; }
  
  size_t getTileCount() const { return tiles.size(); }
};
