#pragma once

#include <algorithm>
#include <cstddef>
#include <array>
#include <cstdint>

// Stable ordering for saves, diagnostics and native alert actions.
enum class CityAlertType : size_t { Power, Water, Unemployment, Services, Budget, Landfill };
struct CityAlertRecord {
  bool active = false;
  uint32_t badTicks = 0;
  uint32_t goodTicks = 0;
  uint32_t sinceTick = 0;
  uint32_t lastNoticeTick = 0;
};
struct CityAlertInputs {
  bool hasBuildings = false;
  bool hasResidents = false;
  bool requireUtilities = true;
  float powerCoverage = 1.f;
  float powerSupply = 1.f;
  float waterCoverage = 1.f;
  float waterSupply = 1.f;
  float unemployment = 0.f;
  float serviceCoverage = 1.f;
  int64_t revenue = 0;
  int64_t expenses = 0;
  uint32_t fullLandfills = 0;
  int landfillX = 0;
  int landfillY = 0;
  int64_t landfillRemaining = 0;
  int64_t landfillFillRate = 0;
};
struct CityAlerts {
  static constexpr uint32_t cooldown = 60;
  uint32_t checkedTick = 0;
  CityAlertInputs inputs;
  std::array<CityAlertRecord, 6> records{};
  static const char* name(size_t i) {
    const char* names[] = {"POWER SHORTAGE", "WATER SHORTAGE", "UNEMPLOYMENT", "SERVICE GAPS", "BUDGET DEFICIT", "LANDFILL FULL"};
    return i < 6 ? names[i] : "UNKNOWN";
  }
  static const char* action(size_t i) {
    const char* actions[] = {"SERVICE OVERLAY", "SERVICE OVERLAY", "DEMAND OVERLAY", "SERVICE OVERLAY", "TAXES AND BUDGET", "WASTE SERVICES"};
    return i < 6 ? actions[i] : "UNKNOWN";
  }
  void update(uint32_t tick, const CityAlertInputs& next) {
    if (tick <= checkedTick) return;
    checkedTick = tick;
    inputs = next;
    const float power = std::min(next.powerCoverage, next.powerSupply);
    const float water = std::min(next.waterCoverage, next.waterSupply);
    const bool utility = next.hasBuildings && next.requireUtilities;
    const std::array<bool, 6> bad{{utility && power < .95f, utility && water < .95f,
      next.hasResidents && next.unemployment > .20f,
      next.hasBuildings && next.serviceCoverage < .50f, next.expenses > next.revenue, next.fullLandfills > 0}};
    const std::array<bool, 6> good{{!utility || power >= .99f, !utility || water >= .99f,
      !next.hasResidents || next.unemployment <= .15f,
      !next.hasBuildings || next.serviceCoverage >= .65f, next.expenses <= next.revenue, next.fullLandfills == 0}};
    for (size_t i = 0; i < records.size(); ++i) {
      auto& r = records[i];
      r.badTicks = bad[i] ? std::min(3u, r.badTicks + 1) : 0;
      r.goodTicks = good[i] ? std::min(2u, r.goodTicks + 1) : 0;
      if (!r.active && r.badTicks == 3) { r.active = true; r.sinceTick = tick - 2; }
      if (r.active && r.goodTicks == 2) { r.active = false; r.sinceTick = 0; }
      if (r.active && (r.lastNoticeTick == 0 || tick - r.lastNoticeTick >= cooldown))
        r.lastNoticeTick = tick;
    }
  }
  bool hasNotice() const {
    if (checkedTick == 0) return false;
    for (const auto& r : records) if (r.active && r.lastNoticeTick == checkedTick) return true;
    return false;
  }
  size_t activeCount() const {
    return static_cast<size_t>(std::count_if(records.begin(), records.end(), [](const auto& r) { return r.active; }));
  }
};
