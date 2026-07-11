#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

#include "src/visualization/MapRenderer.hpp"
#include "src/networks/RoadNetwork.hpp"

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
