#include <filesystem>
#include <fstream>
#include <vector>

#include "gtest/gtest.h"

#include "src/entities/EntityStore.hpp"
#include "src/networks/RoadNetwork.hpp"
#include "src/systems/GrowthSystem.hpp"
#include "src/visualization/MapRenderer.hpp"

TEST(MapRendererTests, WritesPpmWithExpectedDimensions) {
  CityMap map({6, 4});
  RoadNetwork roads(map);
  EntityStore store;

  map.getTile({1, 1}).zone = 1;
  roads.buildRoad({1, 1}, {2, 1});
  EntityId b = store.createBuilding(BuildingType::Residential, {1, 1}, 8);
  map.getTile({1, 1}).buildingId = static_cast<uint32_t>(b);

  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_map_renderer.ppm";

  RenderOptions options;
  options.tilePixels = 4;

  ASSERT_TRUE(MapRenderer::renderToPPM(filePath.string(), map, store, options));

  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());

  char magic[3] = {0, 0, 0};
  int width = 0;
  int height = 0;
  int maxValue = 0;
  in >> magic >> width >> height >> maxValue;

  EXPECT_STREQ(magic, "P6");
  EXPECT_EQ(width, 24);   // 6 * 4
  EXPECT_EQ(height, 16);  // 4 * 4
  EXPECT_EQ(maxValue, 255);

  // Windows refuses to delete a file that still has an open handle.
  in.close();
  std::filesystem::remove(filePath);
}

TEST(MapRendererTests, ViewportClampsAndRendersSubset) {
  CityMap map({10, 10});
  EntityStore store;

  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_map_renderer_subset.ppm";

  RenderOptions options;
  options.tilePixels = 3;
  options.viewX = 8;
  options.viewY = 8;
  options.viewWidth = 5;
  options.viewHeight = 5;

  ASSERT_TRUE(MapRenderer::renderToPPM(filePath.string(), map, store, options));

  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());

  char magic[3] = {0, 0, 0};
  int width = 0;
  int height = 0;
  int maxValue = 0;
  in >> magic >> width >> height >> maxValue;

  EXPECT_STREQ(magic, "P6");
  EXPECT_EQ(width, 6);   // clamped to 2 tiles * 3 pixels
  EXPECT_EQ(height, 6);  // clamped to 2 tiles * 3 pixels
  EXPECT_EQ(maxValue, 255);

  // Windows refuses to delete a file that still has an open handle.
  in.close();
  std::filesystem::remove(filePath);
}

TEST(MapRendererTests, HigherCapacityBuildingFillsMoreOfTheTile) {
  CityMap map({2, 1});
  EntityStore store;
  map.getTile({0, 0}).zone = 1;
  map.getTile({1, 0}).zone = 1;
  map.getTile({0, 0}).buildingId = store.createBuilding(BuildingType::Residential, {0, 0}, 16);
  map.getTile({1, 0}).buildingId = store.createBuilding(BuildingType::Residential, {1, 0}, 128);
  ASSERT_EQ(buildingDensityTier(BuildingType::Residential, 16), 1);
  ASSERT_EQ(buildingDensityTier(BuildingType::Residential, 128), 4);

  const std::filesystem::path filePath =
    std::filesystem::temp_directory_path() / "urban_sim_core_map_renderer_density.ppm";
  RenderOptions options;
  options.tilePixels = 10;
  ASSERT_TRUE(MapRenderer::renderToPPM(filePath.string(), map, store, options));

  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());
  std::string magic;
  int width = 0;
  int height = 0;
  int maxValue = 0;
  in >> magic >> width >> height >> maxValue;
  ASSERT_EQ(magic, "P6");
  ASSERT_EQ(width, 20);
  ASSERT_EQ(height, 10);
  in.get();

  const int pixelCount = width * height * 3;
  std::vector<char> pixels(static_cast<size_t>(pixelCount));
  in.read(pixels.data(), pixelCount);
  ASSERT_EQ(in.gcount(), pixelCount);

  auto buildingPixels = [&](int tileX) {
    int count = 0;
    for (int y = 0; y < height; ++y) {
      for (int x = tileX * 10; x < tileX * 10 + 10; ++x) {
        const int index = (y * width + x) * 3;
        const auto r = static_cast<unsigned char>(pixels[static_cast<size_t>(index)]);
        const auto g = static_cast<unsigned char>(pixels[static_cast<size_t>(index + 1)]);
        const auto b = static_cast<unsigned char>(pixels[static_cast<size_t>(index + 2)]);
        if (r == 44 && g == 132 && b == 70) {
          ++count;
        }
      }
    }
    return count;
  };

  EXPECT_EQ(buildingPixels(0), 64);
  EXPECT_EQ(buildingPixels(1), 100);

  in.close();
  std::filesystem::remove(filePath);
}
