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

#include "src/visualization/VisualizerTypes.hpp"


namespace visualizer {

bool gReadableUiText = true;




const char* overlayModeName(OverlayMode mode) {
  switch (mode) {
    case OverlayMode::Zone:
      return "zone";
    case OverlayMode::LandValue:
      return "land-value";
    case OverlayMode::Pollution:
      return "pollution";
    case OverlayMode::ServiceCoverage:
      return "service";
    case OverlayMode::TrafficCongestion:
      return "traffic";
    case OverlayMode::Demand:
      return "demand";
    case OverlayMode::Happiness:
      return "happiness";
    case OverlayMode::RouteHeatmap:
      return "route-heat";
    default:
      return "unknown";
  }
}


const std::array<OverlayMode, 8>& overlayModes() {
  static const std::array<OverlayMode, 8> modes = {
    OverlayMode::Zone,
    OverlayMode::LandValue,
    OverlayMode::Pollution,
    OverlayMode::ServiceCoverage,
    OverlayMode::TrafficCongestion,
    OverlayMode::Demand,
    OverlayMode::Happiness,
    OverlayMode::RouteHeatmap,
  };
  return modes;
}

bool overlayHitTest(int mouseX, int mouseY, OverlayMode& outMode) {
  const auto& modes = overlayModes();
  for (size_t i = 0; i < modes.size(); ++i) {
    const SDL_Rect rect{
      kOverlayKeyStartX + static_cast<int>(i) * (kOverlayKeyWidth + kOverlayKeyGap),
      kOverlayKeyY,
      kOverlayKeyWidth,
      kOverlayKeyHeight
    };
    if (mouseX >= rect.x && mouseX < rect.x + rect.w
        && mouseY >= rect.y && mouseY < rect.y + rect.h) {
      outMode = modes[i];
      return true;
    }
  }
  return false;
}

ZoneType nextPlayableZone(ZoneType zone) {
  switch (zone) {
    case ZoneType::Residential: return ZoneType::Commercial;
    case ZoneType::Commercial: return ZoneType::Industrial;
    case ZoneType::Industrial: return ZoneType::Office;
    case ZoneType::Office:
    default: return ZoneType::Residential;
  }
}

ServiceType nextPlayableService(ServiceType type) {
  switch (type) {
    case ServiceType::Fire: return ServiceType::Police;
    case ServiceType::Police: return ServiceType::Health;
    case ServiceType::Health: return ServiceType::Education;
    case ServiceType::Education: return ServiceType::Power;
    case ServiceType::Power: return ServiceType::Water;
    case ServiceType::Water: return ServiceType::Sanitation;
    case ServiceType::Sanitation: return ServiceType::Garbage;
    case ServiceType::Garbage: return ServiceType::Recycling;
    case ServiceType::Recycling: return ServiceType::Cemetery;
    case ServiceType::Cemetery: return ServiceType::Crematorium;
    case ServiceType::Crematorium:
    default: return ServiceType::Fire;
  }
}

RGB serviceFacilityColor(ServiceType type) {
  switch (type) {
    case ServiceType::Fire: return {220, 55, 45};
    case ServiceType::Police: return {50, 105, 220};
    case ServiceType::Health: return {235, 235, 245};
    case ServiceType::Education: return {235, 195, 55};
    case ServiceType::Power: return {255, 225, 70};
    case ServiceType::Water: return {55, 190, 235};
    case ServiceType::Sanitation: return {95, 175, 105};
    case ServiceType::Garbage: return {120, 95, 70};
    case ServiceType::Recycling: return {55, 205, 125};
    case ServiceType::Cemetery: return {115, 125, 105};
    case ServiceType::Crematorium: return {175, 105, 185};
    default: return {180, 180, 180};
  }
}

uint8_t toByte(float normalized) {
  const float clamped = std::max(0.0f, std::min(1.0f, normalized));
  return static_cast<uint8_t>(clamped * 255.0f);
}

RGB zoneColor(int zone) {
  switch (zone) {
    case 1:
      return {119, 221, 119};
    case 2:
      return {100, 149, 237};
    case 3:
      return {255, 179, 71};
    case 4:
      return {80, 200, 120};
    case 5:
      return {147, 112, 219};
    default:
      return {190, 207, 181};
  }
}

RGB terrainTint(const Tile& tile, RGB base) {
  if (tile.type == 2) {
    return {68, 137, 218};
  }
  if (tile.type == 1) {
    base.r = static_cast<uint8_t>(std::max(0, base.r - 25));
    base.g = static_cast<uint8_t>(std::max(0, base.g - 25));
    base.b = static_cast<uint8_t>(std::max(0, base.b - 25));
  }
  return base;
}

RGB landValueColor(float landValue) {
  const float normalized = (landValue - 50.0f) / 150.0f;
  const uint8_t value = toByte(normalized);
  return {value, static_cast<uint8_t>(255 - value / 2), static_cast<uint8_t>(255 - value)};
}

RGB pollutionColor(float pollution) {
  const uint8_t value = toByte(pollution);
  return {value, static_cast<uint8_t>(180 - value / 2), static_cast<uint8_t>(80 - value / 4)};
}

RGB serviceCoverageColor(float score) {
  const uint8_t value = toByte(score);
  return {static_cast<uint8_t>(255 - value), static_cast<uint8_t>(80 + (value / 2)), value};
}

RGB congestionColor(float score) {
  const uint8_t value = toByte(score);
  return {value, static_cast<uint8_t>(255 - value), static_cast<uint8_t>(80 - value / 4)};
}

RGB demandColor(float score) {
  const uint8_t value = toByte(score);
  return {static_cast<uint8_t>(120 + value / 2), static_cast<uint8_t>(80 + value / 3), static_cast<uint8_t>(255 - value / 3)};
}

RGB happinessColor(float score) {
  const uint8_t value = toByte(score);
  return {static_cast<uint8_t>(255 - value / 2), static_cast<uint8_t>(120 + value / 2), static_cast<uint8_t>(80 + value / 4)};
}

RGB routeHeatColor(float score) {
  const uint8_t value = toByte(score);
  const uint8_t warm = static_cast<uint8_t>(80 + (value * 3) / 5);
  return {value, warm, static_cast<uint8_t>(255 - value / 2)};
}


}  // namespace visualizer
