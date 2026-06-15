#include "src/world/TerrainGenerator.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

#include "src/core/Random.hpp"

TerrainStats TerrainGenerator::generate(CityMap& map, uint32_t seed, const TerrainParams& params) {
  TerrainStats stats;

  const glm::ivec2 dims = map.getDimensions();
  const int width = dims.x;
  const int height = dims.y;
  const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (count == 0) {
    return stats;
  }

  DeterministicRandom rng(seed);

  // 1. Seed a per-tile random height field.
  std::vector<int> field(count);
  for (size_t i = 0; i < count; ++i) {
    field[i] = static_cast<int>(rng.integer(0, 255));
  }

  // 2. Box-blur so low/high regions form coherent basins and ridges rather
  //    than salt-and-pepper noise.
  std::vector<int> blurred(count);
  for (int pass = 0; pass < std::max(0, params.smoothingPasses); ++pass) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        int sum = 0;
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
              continue;
            }
            sum += field[static_cast<size_t>(ny) * width + nx];
            ++n;
          }
        }
        blurred[static_cast<size_t>(y) * width + x] = sum / n;
      }
    }
    field.swap(blurred);
  }

  // 3. Order tiles by height (index as a deterministic tiebreaker) so we can
  //    take exact-count lowest tiles as water and highest as terrain.
  std::vector<size_t> order(count);
  std::iota(order.begin(), order.end(), size_t{0});
  std::sort(order.begin(), order.end(), [&field](size_t a, size_t b) {
    if (field[a] != field[b]) {
      return field[a] < field[b];
    }
    return a < b;
  });

  const float waterFrac = std::clamp(params.waterFraction, 0.0f, 1.0f);
  const float terrainFrac = std::clamp(params.terrainFraction, 0.0f, 1.0f);
  size_t waterCount = static_cast<size_t>(waterFrac * static_cast<float>(count));
  size_t terrainCount = static_cast<size_t>(terrainFrac * static_cast<float>(count));
  // Never let the two bands overlap.
  if (waterCount + terrainCount > count) {
    terrainCount = count - waterCount;
  }

  // 4. Reset to empty, then stamp the water (lowest) and terrain (highest) bands.
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      map.getTile({x, y}).type = 0;
    }
  }

  auto coordOf = [width](size_t index) -> Coord {
    return Coord{static_cast<int>(index % static_cast<size_t>(width)),
                 static_cast<int>(index / static_cast<size_t>(width))};
  };

  for (size_t k = 0; k < waterCount; ++k) {
    map.getTile(coordOf(order[k])).type = 2;
  }
  for (size_t k = 0; k < terrainCount; ++k) {
    map.getTile(coordOf(order[count - 1 - k])).type = 1;
  }

  stats.waterTiles = static_cast<uint32_t>(waterCount);
  stats.terrainTiles = static_cast<uint32_t>(terrainCount);
  stats.buildableTiles = static_cast<uint32_t>(count - waterCount - terrainCount);
  return stats;
}
