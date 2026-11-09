#pragma once
#include "src/visualization/Camera3D.hpp"
#include "src/visualization/VisualizerTypes.hpp"
#include "src/visualization/TransitOverlay.hpp"
#include "src/systems/DistrictSystem.hpp"
namespace visualizer {
void drawGroundHighlight(DrawList* list, const Camera3D& camera, Coord tile, RGB color, uint8_t alpha);
void drawCity3D(DrawList* list, const Camera3D& camera, const CityMap& map,
                const RoadNetwork& roads, const EntityStore& store,
                const std::vector<ServiceFacility>& facilities, OverlayMode overlay,
                const std::unordered_map<Coord,float,Vec2Hash>& heat, float penalty,
                const DistrictSystem& districts, DistrictId selectedDistrict,
                const TransitMarks* transit);
}
