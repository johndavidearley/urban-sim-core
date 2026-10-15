#pragma once

#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "src/gameplay/CityHistory.hpp"

inline nlohmann::json cityHistoryJson(const CityHistory& history) {
  auto result = nlohmann::json::array();
  for (const auto& s : history.samples) {
    result.push_back({{"tick", s.tick}, {"population", s.population},
      {"policy_changed", history.policyChanged(result.size())},
      {"arrivals", s.migration.arrivals}, {"departures", s.migration.departures},
      {"deaths", s.migration.deaths}, {"net_migration", s.migration.net},
      {"revenue", s.revenue}, {"expenses", s.expenses}, {"funds", s.funds}, {"debt", s.debt},
      {"tax_percent", s.policy.taxPercent}, {"service_percent", s.policy.servicePercent}});
  }
  return result;
}

inline CityHistory parseCityHistory(const nlohmann::json& rows, uint32_t tick) {
  if (!rows.is_array() || rows.size() > CityHistory::capacity)
    throw std::runtime_error("Invalid city history size");
  CityHistory result;
  for (const auto& row : rows) {
    auto integer = [&](const char* key, int64_t lo, int64_t hi) {
      const auto& value = row.at(key);
      if (!value.is_number_integer() ||
          (value.is_number_unsigned() && value.get<uint64_t>() > static_cast<uint64_t>(hi)))
        throw std::runtime_error("Invalid city history integer");
      const auto n = value.get<int64_t>();
      if (n < lo || n > hi) throw std::runtime_error("Invalid city history range");
      return n;
    };
    CityHistorySample s;
    const auto max = std::numeric_limits<int64_t>::max();
    const auto u32 = std::numeric_limits<uint32_t>::max();
    s.tick = static_cast<uint32_t>(integer("tick", 1, u32));
    if (s.tick > tick || (!result.samples.empty() &&
        static_cast<uint64_t>(result.samples.back().tick) + 1 != s.tick))
      throw std::runtime_error("Invalid city history tick order");
    s.population = static_cast<uint32_t>(integer("population", 0, u32));
    s.migration.arrivals = static_cast<uint32_t>(integer("arrivals", 0, u32));
    s.migration.departures = static_cast<uint32_t>(integer("departures", 0, u32));
    s.migration.deaths = static_cast<uint32_t>(integer("deaths", 0, u32));
    s.migration.net = integer("net_migration", -static_cast<int64_t>(u32), u32);
    if (s.migration.net != static_cast<int64_t>(s.migration.arrivals) - s.migration.departures - s.migration.deaths)
      throw std::runtime_error("Inconsistent city history migration");
    s.revenue = integer("revenue", 0, max);
    s.expenses = integer("expenses", 0, max);
    s.funds = integer("funds", 0, max);
    s.debt = integer("debt", 0, max);
    auto policy = [&](const char* key, auto& values, int limit) {
      const auto& array = row.at(key);
      if (!array.is_array() || array.size() != values.size())
        throw std::runtime_error("Invalid city history policy size");
      for (size_t i = 0; i < values.size(); ++i) {
        if (!array[i].is_number_integer() || array[i].get<double>() < 0 || array[i].get<double>() > limit)
          throw std::runtime_error("Invalid city history policy");
        values[i] = array[i].get<int>();
      }
    };
    policy("tax_percent", s.policy.taxPercent, 20);
    policy("service_percent", s.policy.servicePercent, 100);
    result.append(s);
  }
  if (!result.samples.empty() && result.samples.back().tick != tick)
    throw std::runtime_error("City history must end at the completed tick");
  return result;
}
