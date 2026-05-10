#pragma once

#include <cstdint>
#include <string>
#include "src/world/CityMap.hpp"

enum class ZoneType : int {
  None = 0,
  Residential = 1,
  Commercial = 2,
  Industrial = 3,
  Park = 4
};

struct ZoneDemand {
  float residential = 0.0f;
  float commercial = 0.0f;
  float industrial = 0.0f;
};

class Zoning {
public:
  static bool parseZoneType(const std::string& raw, ZoneType& outZone);
  static const char* zoneToString(int zone);
  static char zoneToSymbol(int zone);

  static bool applyZoneRect(
    CityMap& map,
    Coord a,
    Coord b,
    ZoneType zone,
    int* zonedCount = nullptr
  );

  static ZoneDemand calculateDemand(uint32_t seed);
  static float defaultLandValueForZone(ZoneType zone);
};
