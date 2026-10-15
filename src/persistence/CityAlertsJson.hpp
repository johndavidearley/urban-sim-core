#pragma once

#include <cmath>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "src/gameplay/CityAlerts.hpp"

inline nlohmann::json cityAlertsJson(const CityAlerts& alerts) {
  const auto& v = alerts.inputs;
  auto rows = nlohmann::json::array();
  for (size_t i = 0; i < alerts.records.size(); ++i) {
    const auto& r = alerts.records[i];
    rows.push_back({{"name", CityAlerts::name(i)}, {"action", CityAlerts::action(i)},
      {"active", r.active}, {"bad_ticks", r.badTicks}, {"good_ticks", r.goodTicks},
      {"since_tick", r.sinceTick}, {"last_notice_tick", r.lastNoticeTick}});
  }
  return {{"checked_tick", alerts.checkedTick}, {"records", rows}, {"inputs", {
    {"has_buildings", v.hasBuildings}, {"has_residents", v.hasResidents}, {"require_utilities", v.requireUtilities},
    {"power_coverage", v.powerCoverage}, {"power_supply", v.powerSupply},
    {"water_coverage", v.waterCoverage}, {"water_supply", v.waterSupply},
    {"unemployment", v.unemployment}, {"service_coverage", v.serviceCoverage},
    {"revenue", v.revenue}, {"expenses", v.expenses}}}};
}
inline CityAlerts parseCityAlerts(const nlohmann::json& root, uint32_t tick) {
  auto integer = [](const nlohmann::json& value, uint64_t maximum) -> uint64_t {
    if (!value.is_number_integer() || value.get<double>() < 0 ||
        (value.is_number_unsigned() && value.get<uint64_t>() > maximum))
      throw std::runtime_error("Invalid city alert integer");
    const auto n = value.get<uint64_t>();
    if (n > maximum) throw std::runtime_error("Invalid city alert range");
    return n;
  };
  CityAlerts result;
  result.checkedTick = static_cast<uint32_t>(integer(root.at("checked_tick"), tick));
  if (result.checkedTick != 0 && result.checkedTick != tick)
    throw std::runtime_error("City alert check must match completed tick");
  const auto& values = root.at("inputs");
  auto& v = result.inputs;
  v.hasBuildings = values.at("has_buildings").get<bool>();
  v.hasResidents = values.at("has_residents").get<bool>();
  v.requireUtilities = values.at("require_utilities").get<bool>();
  auto ratio = [&](const char* key) {
    if (!values.at(key).is_number()) throw std::runtime_error("Invalid city alert measurement");
    const auto n = values.at(key).get<float>();
    if (!std::isfinite(n) || n < 0 || n > 1) throw std::runtime_error("Invalid city alert ratio");
    return n;
  };
  v.powerCoverage = ratio("power_coverage"); v.powerSupply = ratio("power_supply");
  v.waterCoverage = ratio("water_coverage"); v.waterSupply = ratio("water_supply");
  v.unemployment = ratio("unemployment"); v.serviceCoverage = ratio("service_coverage");
  v.revenue = static_cast<int64_t>(integer(values.at("revenue"), INT64_MAX));
  v.expenses = static_cast<int64_t>(integer(values.at("expenses"), INT64_MAX));
  const auto& rows = root.at("records");
  if (!rows.is_array() || rows.size() != result.records.size())
    throw std::runtime_error("Invalid city alert count");
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto& row = rows[i]; auto& r = result.records[i];
    r.active = row.at("active").get<bool>();
    r.badTicks = static_cast<uint32_t>(integer(row.at("bad_ticks"), 3));
    r.goodTicks = static_cast<uint32_t>(integer(row.at("good_ticks"), 2));
    r.sinceTick = static_cast<uint32_t>(integer(row.at("since_tick"), result.checkedTick));
    r.lastNoticeTick = static_cast<uint32_t>(integer(row.at("last_notice_tick"), result.checkedTick));
    if ((r.active && (r.sinceTick == 0 || r.goodTicks == 2)) || (!r.active && r.sinceTick != 0) ||
        (result.checkedTick == 0 && (r.active || r.badTicks || r.goodTicks || r.lastNoticeTick)))
      throw std::runtime_error("Inconsistent city alert state");
  }
  return result;
}
