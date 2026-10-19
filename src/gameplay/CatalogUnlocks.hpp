#pragma once

#include "src/gameplay/PopulationMilestones.hpp"
#include "src/systems/ServiceSystem.hpp"
#include "src/world/Zoning.hpp"

// Player catalog unlock policy. Both native and MCP actions consult this
// policy; autonomous simulation does not enforce player unlocks.
namespace catalog {
inline constexpr uint32_t arterials = 500;
inline constexpr uint32_t districts = 500;
inline constexpr uint32_t buses = 1200;
inline constexpr uint32_t heightCaps = 1200;
inline constexpr uint32_t bonds = 1200;
inline constexpr uint32_t terminals = 5000;
inline constexpr uint32_t hotels = 5000;
inline constexpr uint32_t airports = 10000;

inline uint32_t zone(ZoneType type) {
  return type == ZoneType::Park ? 500u : type == ZoneType::Office ? 1200u : 0u;
}

inline uint32_t service(ServiceType type, PowerSourceType source, bool treatment) {
  if (type == ServiceType::Power) {
    switch (source) {
      case PowerSourceType::NaturalGas:
      case PowerSourceType::Solar: return 1200;
      case PowerSourceType::Hydro:
      case PowerSourceType::Substation: return 2500;
      case PowerSourceType::Nuclear: return 10000;
      default: return 0;
    }
  }
  switch (type) {
    case ServiceType::Cemetery:
    case ServiceType::Recycling: return 500;
    case ServiceType::Crematorium:
    case ServiceType::Incinerator: return 2500;
    case ServiceType::Sanitation: return treatment ? 2500u : 0u;
    default: return 0;
  }
}

inline std::string lockedReason(const PopulationMilestones& milestones, uint32_t threshold) {
  return milestones.unlocks(threshold) ? std::string{} :
    "LOCKED - REACH " + std::to_string(threshold) + " RESIDENTS";
}

inline const char* unlockedSummary(uint32_t threshold) {
  switch (threshold) {
    case 500: return "ARTERIALS PARKS DISTRICTS CEMETERY RECYCLING";
    case 1200: return "BUS OFFICE GAS SOLAR HEIGHT CAPS BONDS";
    case 2500: return "SUBSTATION HYDRO TREATMENT CREMATORIUM INCINERATOR";
    case 5000: return "CARGO PASSENGER TERMINALS HOTELS";
    case 10000: return "NUCLEAR POWER AIRPORT";
    default: return "LOCAL ROADS RCI BASIC POWER AND SERVICES";
  }
}
} // namespace catalog
