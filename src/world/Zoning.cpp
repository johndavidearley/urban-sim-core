#include "src/world/Zoning.hpp"

#include <algorithm>
#include <cctype>

#include "src/core/Random.hpp"

namespace {
std::string toUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}
} // namespace

bool Zoning::parseZoneType(const std::string& raw, ZoneType& outZone) {
  const std::string zone = toUpper(raw);
  if (zone == "NONE" || zone == "EMPTY") {
    outZone = ZoneType::None;
    return true;
  }
  if (zone == "RES" || zone == "RESIDENTIAL") {
    outZone = ZoneType::Residential;
    return true;
  }
  if (zone == "COM" || zone == "COMMERCIAL") {
    outZone = ZoneType::Commercial;
    return true;
  }
  if (zone == "IND" || zone == "INDUSTRIAL") {
    outZone = ZoneType::Industrial;
    return true;
  }
  if (zone == "PARK") {
    outZone = ZoneType::Park;
    return true;
  }
  if (zone == "OFF" || zone == "OFFICE") {
    outZone = ZoneType::Office;
    return true;
  }

  return false;
}

const char* Zoning::zoneToString(int zone) {
  switch (static_cast<ZoneType>(zone)) {
    case ZoneType::None:
      return "None";
    case ZoneType::Residential:
      return "Residential";
    case ZoneType::Commercial:
      return "Commercial";
    case ZoneType::Industrial:
      return "Industrial";
    case ZoneType::Park:
      return "Park";
    case ZoneType::Office:
      return "Office";
    default:
      return "Unknown";
  }
}

char Zoning::zoneToSymbol(int zone) {
  switch (static_cast<ZoneType>(zone)) {
    case ZoneType::None:
      return '.';
    case ZoneType::Residential:
      return 'R';
    case ZoneType::Commercial:
      return 'C';
    case ZoneType::Industrial:
      return 'I';
    case ZoneType::Park:
      return 'P';
    case ZoneType::Office:
      return 'O';
    default:
      return '?';
  }
}

bool Zoning::applyZoneRect(CityMap& map, Coord a, Coord b, ZoneType zone, int* zonedCount) {
  Coord minCorner{std::min(a.x, b.x), std::min(a.y, b.y)};
  Coord maxCorner{std::max(a.x, b.x), std::max(a.y, b.y)};

  if (!map.isValid(minCorner) || !map.isValid(maxCorner)) {
    return false;
  }

  int applied = 0;
  for (int y = minCorner.y; y <= maxCorner.y; ++y) {
    for (int x = minCorner.x; x <= maxCorner.x; ++x) {
      if (map.getTile({x, y}).type == 2) {  // cannot zone water
        continue;
      }
      map.setZone({x, y}, static_cast<int>(zone));
      map.landValue({x, y}) = defaultLandValueForZone(zone);
      ++applied;
    }
  }

  if (zonedCount != nullptr) {
    *zonedCount = applied;
  }

  return true;
}

ZoneDemand Zoning::calculateDemand(uint32_t seed) {
  DeterministicRandom rng(seed);
  ZoneDemand demand;
  demand.residential = rng.uniform(0.0f, 1.0f);
  demand.commercial = rng.uniform(0.0f, 1.0f);
  demand.industrial = rng.uniform(0.0f, 1.0f);
  demand.office = rng.uniform(0.0f, 1.0f);
  return demand;
}

float Zoning::defaultLandValueForZone(ZoneType zone) {
  switch (zone) {
    case ZoneType::Residential:
      return 120.0f;
    case ZoneType::Commercial:
      return 140.0f;
    case ZoneType::Industrial:
      return 90.0f;
    case ZoneType::Park:
      return 130.0f;
    case ZoneType::Office:
      return 150.0f;  // premium: office space commands the highest base value
    case ZoneType::None:
    default:
      return 100.0f;
  }
}
