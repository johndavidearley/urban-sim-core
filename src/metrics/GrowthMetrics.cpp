#include "src/metrics/GrowthMetrics.hpp"

#include <iomanip>
#include <sstream>

#include "src/world/Zoning.hpp"

namespace {
float fillRate(int built, int zoned) {
  if (zoned <= 0) {
    return 0.0f;
  }
  return static_cast<float>(built) / static_cast<float>(zoned);
}
} // namespace

GrowthMetrics GrowthMetrics::collect(const CityMap& map, const EntityStore& store) {
  GrowthMetrics metrics;
  const glm::ivec2 dims = map.getDimensions();

  for (int y = 0; y < dims.y; ++y) {
    for (int x = 0; x < dims.x; ++x) {
      const Tile& tile = map.getTile({x, y});
      const ZoneType zone = static_cast<ZoneType>(map.zone({x, y}));
      const bool hasBuilding = tile.buildingId != EntityIdUtils::NullEntity;

      if (zone == ZoneType::Residential) {
        ++metrics.zonedResidential;
        if (hasBuilding) {
          ++metrics.builtResidential;
        }
      } else if (zone == ZoneType::Commercial) {
        ++metrics.zonedCommercial;
        if (hasBuilding) {
          ++metrics.builtCommercial;
        }
      } else if (zone == ZoneType::Industrial) {
        ++metrics.zonedIndustrial;
        if (hasBuilding) {
          ++metrics.builtIndustrial;
        }
      }
    }
  }

  metrics.totalBuildings = static_cast<int>(store.getBuildingCount());
  metrics.residentialFillRate = fillRate(metrics.builtResidential, metrics.zonedResidential);
  metrics.commercialFillRate = fillRate(metrics.builtCommercial, metrics.zonedCommercial);
  metrics.industrialFillRate = fillRate(metrics.builtIndustrial, metrics.zonedIndustrial);

  return metrics;
}

std::string GrowthMetrics::toString() const {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(1);
  ss << "Growth Summary:\n";
  ss << "  Buildings total: " << totalBuildings << "\n";
  ss << "  Residential: " << builtResidential << "/" << zonedResidential
     << " (" << (residentialFillRate * 100.0f) << "%)\n";
  ss << "  Commercial:  " << builtCommercial << "/" << zonedCommercial
     << " (" << (commercialFillRate * 100.0f) << "%)\n";
  ss << "  Industrial:  " << builtIndustrial << "/" << zonedIndustrial
     << " (" << (industrialFillRate * 100.0f) << "%)\n";
  return ss.str();
}
