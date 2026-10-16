#include "src/persistence/GameplaySessionSystem.hpp"
#include "src/persistence/CityHistoryJson.hpp"
#include "src/persistence/TransitReportJson.hpp"
#include "src/persistence/RegionalConnectionsJson.hpp"
#include "src/persistence/RegionalSettlementJson.hpp"
#include "src/persistence/RegionalReservations.hpp"
#include "src/persistence/TourismJson.hpp"
#include "src/persistence/CityAlertsJson.hpp"

#include <cmath>
#include <climits>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <thread>
#include "src/core/ThreadPool.hpp"
#include "src/systems/CitySimulator.hpp"
#include "src/systems/CitySimSupport.hpp"
#include "src/systems/PlayableCityTick.hpp"
#include <exception>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>

#include <nlohmann/json.hpp>

#include "src/persistence/SaveLoadSystem.hpp"
#include "src/systems/DistrictSystem.hpp"

using nlohmann::json;

namespace {
constexpr int kGameplaySessionVersion = 1;

std::string snapshotPath(const json& root, const std::string& sessionPath) {
  const auto raw = root.value("citySnapshot", sessionPath + ".city.json");
  const std::filesystem::path path(raw);
  if (path.is_relative()) {
    const auto adjacent = std::filesystem::path(sessionPath).parent_path() / path;
    if (std::filesystem::exists(adjacent)) return adjacent.string();
  }
  return raw; // Legacy saves sometimes stored a path relative to the process.
}

std::string cityPathFor(const std::string& sessionPath) {
  return sessionPath + ".city.json";
}

bool fail(std::string* errorMessage, const std::string& message) {
  if (errorMessage != nullptr) {
    *errorMessage = message;
  }
  return false;
}

bool validateHotelCivicSites(const CitySnapshot& snapshot,
    const std::vector<Coord>& civicSites, std::string* error) {
  std::set<std::pair<int,int>> positions;
  for (const auto& building : snapshot.buildings)
    if (building.hotelBeds) positions.emplace(building.position.x,building.position.y);
  for (auto site : civicSites)
    if (positions.count({site.x,site.y}))
      return fail(error,"hotel overlaps a civic facility");
  return true;
}

bool validServiceType(int value) {
  return value >= static_cast<int>(ServiceType::Fire)
      && value <= static_cast<int>(ServiceType::Crematorium);
}

bool validTransitMode(int value) {
  return value == static_cast<int>(TransitMode::Bus)
      || value == static_cast<int>(TransitMode::Rail);
}

bool validDistrictArchetype(int value) {
  return value >= static_cast<int>(DistrictArchetype::General)
      && value <= static_cast<int>(DistrictArchetype::TechHub);
}

json serializeDistricts(const std::vector<GameplaySessionState::DistrictRecord>& districts) {
  json out = json::array();
  for (const auto& district : districts) {
    out.push_back({
      {"name", district.name},
      {"x1", district.x1},
      {"y1", district.y1},
      {"x2", district.x2},
      {"y2", district.y2},
      {"archetype", district.archetype},
      {"serviceBudgetCap", district.serviceBudgetCap}
    });
  }
  return out;
}

bool parseDistricts(
  const json& items,
  const CityMap& map,
  std::vector<GameplaySessionState::DistrictRecord>& out,
  std::string* errorMessage
) {
  for (const json& item : items) {
    GameplaySessionState::DistrictRecord district;
    district.name = item.value("name", std::string{});
    district.x1 = item.at("x1").get<int>();
    district.y1 = item.at("y1").get<int>();
    district.x2 = item.at("x2").get<int>();
    district.y2 = item.at("y2").get<int>();
    district.archetype = item.value("archetype", 0);
    district.serviceBudgetCap = item.value("serviceBudgetCap", int64_t{-1});
    if (district.x1 > district.x2 || district.y1 > district.y2
        || !map.isValid({district.x1, district.y1})
        || !map.isValid({district.x2, district.y2})) {
      return fail(errorMessage, "gameplay session contains an invalid district");
    }
    if (!validDistrictArchetype(district.archetype)) {
      return fail(errorMessage, "gameplay session contains an invalid district archetype");
    }
    out.push_back(std::move(district));
  }
  return true;
}

json serializeTransitRoutes(const std::vector<TransitRoute>& routes) {
  json out = json::array();
  for (const TransitRoute& route : routes) {
    json stops = json::array();
    for (const Coord& stop : route.stops) {
      stops.push_back({{"x", stop.x}, {"y", stop.y}});
    }
    out.push_back({
      {"id", route.id},
      {"mode", static_cast<int>(route.mode)},
      {"vehicleCount", route.vehicleCount},
      {"capacityPerVehicle", route.capacityPerVehicle},
      {"stopCoverageRadius", route.stopCoverageRadius},
      {"stops", stops}
    });
  }
  return out;
}

bool parseTransitRoutes(
  const json& items,
  const CityMap& map,
  std::vector<TransitRoute>& out,
  std::string* errorMessage
) {
  if (!items.is_array() || items.size()>4096) return fail(errorMessage,"invalid transit route count");
  std::set<TransitRouteId> routeIds;
  for (const json& item : items) {
    for (const char* key : {"id","mode","vehicleCount","capacityPerVehicle","stopCoverageRadius"}) {
      if (item.contains(key) && (!item.at(key).is_number_integer() || item.at(key).get<double>()<0 ||
          item.at(key).get<double>()>(std::string(key)=="id" ? static_cast<double>(UINT32_MAX) : static_cast<double>(INT_MAX))))
        return fail(errorMessage,"invalid transit integer");
    }
    TransitRoute route;
    route.id = item.value("id", uint32_t{0});
    if (!routeIds.insert(route.id).second) return fail(errorMessage,"duplicate transit route ID");
    const int mode = item.value("mode", 0);
    if (!validTransitMode(mode)) {
      return fail(errorMessage, "gameplay session contains an invalid transit mode");
    }
    route.mode = static_cast<TransitMode>(mode);
    route.vehicleCount = item.value("vehicleCount", 2);
    route.capacityPerVehicle = item.value("capacityPerVehicle", 30);
    route.stopCoverageRadius = item.value("stopCoverageRadius", 4);
    if (route.vehicleCount < 0 || route.capacityPerVehicle < 0 || route.stopCoverageRadius < 0 ||
        static_cast<int64_t>(route.vehicleCount) * route.capacityPerVehicle > INT_MAX) {
      return fail(errorMessage, "gameplay session contains invalid transit parameters");
    }
    for (const json& stop : item.at("stops")) {
      const Coord coord{stop.at("x").get<int>(), stop.at("y").get<int>()};
      if (!map.isValid(coord)) {
        return fail(errorMessage, "gameplay session contains an invalid transit stop");
      }
      route.stops.push_back(coord);
    }
    out.push_back(std::move(route));
  }
  return true;
}
} // namespace

bool GameplaySessionSystem::readMapDimensions(
  const std::string& sessionPath,
  int& width,
  int& height,
  std::string* errorMessage
) {
  try {
    std::ifstream in(sessionPath);
    if (!in.is_open()) {
      return fail(errorMessage, "unable to open gameplay session file");
    }
    json root;
    in >> root;
    if (root.value("version", -1) != kGameplaySessionVersion) {
      return fail(errorMessage, "unsupported gameplay session version");
    }
    const std::string cityPath = snapshotPath(root, sessionPath);
    CitySnapshot snapshot;
    SnapshotLoadDiagnostics diagnostics;
    if (!SaveLoadSystem::loadSnapshotFromFile(cityPath, snapshot, &diagnostics)) {
      return fail(errorMessage, diagnostics.errorMessage.empty()
        ? "unable to inspect core city snapshot" : diagnostics.errorMessage);
    }
    width = snapshot.width;
    height = snapshot.height;
    return true;
  } catch (const std::exception& e) {
    return fail(errorMessage, std::string("invalid gameplay session: ") + e.what());
  }
}

bool GameplaySessionSystem::save(
  const std::string& sessionPath,
  const CityMap& map,
  const RoadNetwork& roads,
  const EntityStore& store,
  const PopulationStore& population,
  const GameplaySessionState& session,
  std::string* errorMessage
) {
  try {
    if (!session.nextRegionalConnectionId || session.nextRegionalConnectionId>static_cast<uint64_t>(UINT32_MAX)+1)
      throw std::runtime_error("Invalid next regional connection ID");
    parseRegionalConnections(regionalConnectionsJson(session.regionalConnections),map);
    parseRegionalSettlement(regionalSettlementJson(session.regionalSettlement),session.tick);
    std::vector<Coord> civicSites;
    for (const auto& facility:session.facilities) civicSites.push_back(facility.position);
    const auto snapshot = SaveLoadSystem::captureSnapshot(map,roads,store,population);
    parseTourism(tourismJson(session.tourism,session.tick),session.tick,snapshot,&session.transitReport);
    validateTourismAccounting(session.tourism,session.treasuryLedger);
    if (!validateHotelCivicSites(snapshot,civicSites,errorMessage) ||
        !validateRegionalReservations(snapshot,session.regionalConnections,civicSites,errorMessage)) return false;
  } catch (const std::exception& e) {
    return fail(errorMessage,std::string("invalid regional configuration: ")+e.what());
  }
  if (!SaveLoadSystem::saveToFile(cityPathFor(sessionPath), map, roads, store, population)) {
    return fail(errorMessage, "unable to save core city snapshot");
  }

  json facilities = json::array();
  for (const ServiceFacility& facility : session.facilities) {
    facilities.push_back({
      {"type", static_cast<int>(facility.type)},
      {"x", facility.position.x},
      {"y", facility.position.y},
      {"maxTravelDistance", facility.maxTravelDistance},
      {"quality", facility.quality},
      {"powerSource", static_cast<int>(facility.powerSource)},
      {"powerCapacityMW", facility.powerCapacityMW},
      {"emissionsKgPerMWh", facility.emissionsKgPerMWh},
      {"waterSupplyUnits", facility.waterSupplyUnits}
    });
  }

  json burning = json::array();
  for (const auto& tile : session.disasters.burningTiles)
    burning.push_back({{"x", tile.position.x}, {"y", tile.position.y},
                       {"ticksRemaining", tile.ticksRemaining}});
  const auto& events = session.disasters;
  json disasterState = {{"enabled", session.enableDisasters},
                        {"burningTiles", burning},
                        {"buildingsLostToFire", events.buildingsLostToFire},
                        {"buildingsLostToDisaster", events.buildingsLostToDisaster},
                        {"newIgnitions", events.fire.newIgnitions},
                        {"fireBuildingsDestroyed", events.fire.buildingsDestroyed},
                        {"earthquakeOccurred", events.disaster.earthquakeOccurred},
                        {"floodOccurred", events.disaster.floodOccurred},
                        {"disasterBuildingsDestroyed", events.disaster.buildingsDestroyed}};
  const auto& ledger = session.treasuryLedger;
  const auto& flow = ledger.flow;
  const json ledgerJson = {{"valid", ledger.valid}, {"tick", ledger.tick},
    {"revenue", ledger.breakdown.revenue}, {"expenses", ledger.breakdown.expenses},
    {"tourismFees",ledger.breakdown.tourismFees},{"tourismServices",ledger.breakdown.tourismServices},
    {"totalRevenue", ledger.breakdown.totalRevenue},
    {"totalExpenses", ledger.breakdown.totalExpenses},
    {"fundsBefore", ledger.fundsBefore}, {"fundsAfter", ledger.fundsAfter},
    {"debtBefore", ledger.debtBefore}, {"net", flow.net},
    {"shortfall", flow.shortfall}, {"debtIssued", flow.debtIssued},
    {"debtRepaid", flow.debtRepaid}, {"interestCharged", flow.interestCharged},
    {"outstandingDebt", flow.outstandingDebt}};
  json root = {{"version", kGameplaySessionVersion},
               {"budgetTaxPercent", session.budgetPolicy.taxPercent},
               {"budgetServicePercent", session.budgetPolicy.servicePercent},
               {"treasuryLedger", ledgerJson},
               {"cityHistory", cityHistoryJson(session.history)},
               {"cityAlerts", cityAlertsJson(session.alerts)},
               {"transitReport", transitReportJson(session.transitReport)},
               {"simulationSeed", session.simulationSeed},
               {"disasters", disasterState},
               {"citySnapshot", std::filesystem::absolute(cityPathFor(sessionPath)).string()},
               {"funds", session.funds},
               {"tick", session.tick},
               {"paused", session.paused},
               {"tickIntervalMs", session.tickIntervalMs},
               {"treasuryRevenue", session.treasuryRevenue},
               {"treasuryExpenses", session.treasuryExpenses},
               {"treasuryNet", session.treasuryNet},
               {"treasuryDebt", session.treasuryDebt},
               {"treasuryInterestRemainder", session.treasuryInterestRemainder},
               {"populationTarget", session.populationTarget},
               {"laggedEducationCoverage", session.laggedEducationCoverage},
               {"migrationConditions",
                {{"congestion", session.migrationConditions.congestion},
                 {"pollution", session.migrationConditions.pollution},
                 {"serviceSatisfaction",
                  session.migrationConditions.serviceSatisfaction},
                 {"crimeRate", session.migrationConditions.crimeRate},
                 {"illnessRate", session.migrationConditions.illnessRate},
                 {"utilityAvailability",
                  session.migrationConditions.utilityAvailability}}},
               {"populationChange",
                {{"arrivals", session.populationChange.arrivals},
                 {"departures", session.populationChange.departures},
                 {"deaths", session.populationChange.deaths},
                 {"net", session.populationChange.net},
                 {"desirability", session.populationChange.desirability}}},
               {"fractionalDeaths", session.fractionalDeaths},
               {"awaitingDisposition", session.awaitingDisposition},
               {"autonomousGrowth", session.autonomousGrowth},
               {"autonomousExtent", session.autonomousExtent},
               {"emptyZonedCount", session.emptyZonedCount},
               {"transitRoutes", serializeTransitRoutes(session.transitRoutes)},
               {"regionalConnections", regionalConnectionsJson(session.regionalConnections)},
               {"regionalSettlement", regionalSettlementJson(session.regionalSettlement)},
               {"tourism",tourismJson(session.tourism,session.tick)},
               {"regionalLogisticsEnabled",session.regionalLogisticsEnabled},
               {"nextRegionalConnectionId",[&]() {
                 uint64_t next=std::max<uint64_t>(1,session.nextRegionalConnectionId);
                 for (const auto& c:session.regionalConnections) next=std::max(next,static_cast<uint64_t>(c.id)+1);
                 return next;
               }()},
               {"districts", serializeDistricts(session.districts)},
               {"demand",
                {{"residential", session.demand.residential},
                 {"commercial", session.demand.commercial},
                 {"industrial", session.demand.industrial},
                 {"office", session.demand.office}}},
               {"facilities", facilities}};

  root["growthModifiers"] = json::array();
  for (const auto &modifier : session.growthModifiers) {
    root["growthModifiers"].push_back({{"x1", modifier.minCorner.x},
                                       {"y1", modifier.minCorner.y},
                                       {"x2", modifier.maxCorner.x},
                                       {"y2", modifier.maxCorner.y},
                                       {"multiplier", modifier.multiplier}});
  }
  std::ofstream out(sessionPath);
  if (!out.is_open()) {
    return fail(errorMessage, "unable to open gameplay session file for writing");
  }
  out << root.dump(2);
  return out.good() || (out.flush(), out.good());
}

bool GameplaySessionSystem::load(
  const std::string& sessionPath,
  CityMap& map,
  RoadNetwork& roads,
  EntityStore& store,
  PopulationStore& population,
  GameplaySessionState& session,
  std::string* errorMessage
) {
  try {
    std::ifstream in(sessionPath);
    if (!in.is_open()) {
      return fail(errorMessage, "unable to open gameplay session file");
    }
    json root;
    in >> root;
    if (root.value("version", -1) != kGameplaySessionVersion) {
      return fail(errorMessage, "unsupported gameplay session version");
    }

    GameplaySessionState loaded;
    loaded.simulationSeed = root.value("simulationSeed", uint32_t{98765});
    if (root.contains("disasters")) {
      const auto& item = root.at("disasters");
      auto& events = loaded.disasters;
      loaded.enableDisasters = item.value("enabled", false);
      const auto count = [&](const char* key) {
        const int64_t value = item.value(key, int64_t{0});
        if (value < 0 || value > std::numeric_limits<uint32_t>::max())
          throw std::runtime_error("invalid disaster loss count");
        return static_cast<uint32_t>(value);
      };
      events.buildingsLostToFire = count("buildingsLostToFire");
      events.buildingsLostToDisaster = count("buildingsLostToDisaster");
      events.fire.newIgnitions = count("newIgnitions");
      events.fire.buildingsDestroyed = count("fireBuildingsDestroyed");
      events.disaster.buildingsDestroyed = count("disasterBuildingsDestroyed");
      events.disaster.earthquakeOccurred = item.value("earthquakeOccurred", false);
      events.disaster.floodOccurred = item.value("floodOccurred", false);
      std::set<std::pair<int, int>> positions;
      for (const auto& tile : item.at("burningTiles")) {
        const Coord c{tile.at("x").get<int>(), tile.at("y").get<int>()};
        const int remaining = tile.at("ticksRemaining").get<int>();
        if (!map.isValid(c) || remaining <= 0 ||
            !positions.emplace(c.x, c.y).second)
          return fail(errorMessage, "gameplay session has invalid burning tiles");
        events.burningTiles.push_back({c, remaining});
      }
      events.fire.activeFires = static_cast<uint32_t>(events.burningTiles.size());
      if (events.fire.buildingsDestroyed > events.buildingsLostToFire ||
          events.disaster.buildingsDestroyed > events.buildingsLostToDisaster ||
          events.fire.newIgnitions != events.fire.buildingsDestroyed ||
          (!loaded.enableDisasters && !events.burningTiles.empty()))
        return fail(errorMessage, "gameplay session has inconsistent disasters");
    }
    loaded.funds = root.at("funds").get<int64_t>();
    loaded.tick = root.at("tick").get<uint32_t>();
    if (root.contains("transitReport")) loaded.transitReport = parseTransitReport(root.at("transitReport"),loaded.tick);
    if (root.contains("cityAlerts")) loaded.alerts = parseCityAlerts(root.at("cityAlerts"), loaded.tick);
    if (root.contains("cityHistory")) loaded.history = parseCityHistory(root.at("cityHistory"), loaded.tick);
    if (root.contains("budgetTaxPercent")) {
      const auto& values = root.at("budgetTaxPercent");
      if (!values.is_array() || values.size() != 5)
        return fail(errorMessage, "gameplay session has invalid tax policy");
      for (size_t i = 0; i < 5; ++i) {
        if (!values[i].is_number_integer())
          return fail(errorMessage, "gameplay session has invalid tax policy");
        const auto value = values[i].get<int64_t>();
        if (value < 0 || value > 20)
          return fail(errorMessage, "gameplay session has invalid tax policy");
        loaded.budgetPolicy.taxPercent[i] = static_cast<int>(value);
      }
    }
    if (root.contains("budgetServicePercent")) {
      const auto& values = root.at("budgetServicePercent");
      if (!values.is_array() || values.size() != kServiceTypeCount)
        return fail(errorMessage, "gameplay session has invalid service funding");
      for (size_t i = 0; i < kServiceTypeCount; ++i) {
        if (!values[i].is_number_integer())
          return fail(errorMessage, "gameplay session has invalid service funding");
        const auto value = values[i].get<int64_t>();
        if (value < 0 || value > 100)
          return fail(errorMessage, "gameplay session has invalid service funding");
        loaded.budgetPolicy.servicePercent[i] = static_cast<int>(value);
      }
    }
    if (root.contains("treasuryLedger")) {
      const auto& item = root.at("treasuryLedger");
      auto& ledger = loaded.treasuryLedger;
      ledger.valid = item.at("valid").get<bool>();
      if (ledger.valid) {
        const int64_t tick = item.at("tick").get<int64_t>();
        if (tick < 0 || tick > loaded.tick)
          return fail(errorMessage, "gameplay session has invalid ledger tick");
        ledger.tick = static_cast<uint32_t>(tick);
        if (!item.at("revenue").is_array() || item.at("revenue").size() != 6 ||
            !item.at("expenses").is_array() || item.at("expenses").size() != 5)
          return fail(errorMessage, "gameplay session has invalid ledger categories");
        for (const auto& values : {item.at("revenue"), item.at("expenses")})
          for (const auto& value : values)
            if (!value.is_number_integer())
              return fail(errorMessage, "gameplay session has invalid ledger category amount");
        ledger.breakdown.revenue = item.at("revenue").get<std::array<int64_t, 6>>();
        ledger.breakdown.expenses = item.at("expenses").get<std::array<int64_t, 5>>();
        for (const char* name:{"tourismFees","tourismServices"})
          if (item.contains(name) && (!item.at(name).is_number_integer() ||
              item.at(name).get<double>()<0 || item.at(name).get<double>()>10000))
            return fail(errorMessage,"invalid tourism treasury category");
        ledger.breakdown.tourismFees=item.value("tourismFees",int64_t{0});
        ledger.breakdown.tourismServices=item.value("tourismServices",int64_t{0});
        ledger.breakdown.totalRevenue = item.at("totalRevenue").get<int64_t>();
        ledger.breakdown.totalExpenses = item.at("totalExpenses").get<int64_t>();
        ledger.fundsBefore = item.at("fundsBefore").get<int64_t>();
        ledger.fundsAfter = item.at("fundsAfter").get<int64_t>();
        ledger.debtBefore = item.at("debtBefore").get<int64_t>();
        ledger.flow.revenue = ledger.breakdown.totalRevenue;
        ledger.flow.expenses = ledger.breakdown.totalExpenses;
        ledger.flow.net = item.at("net").get<int64_t>();
        ledger.flow.shortfall = item.at("shortfall").get<int64_t>();
        ledger.flow.debtIssued = item.at("debtIssued").get<int64_t>();
        ledger.flow.debtRepaid = item.at("debtRepaid").get<int64_t>();
        ledger.flow.interestCharged = item.at("interestCharged").get<int64_t>();
        ledger.flow.outstandingDebt = item.at("outstandingDebt").get<int64_t>();
        bool valid = true;
        for (auto value : ledger.breakdown.revenue) valid = valid && value >= 0;
        for (auto value : ledger.breakdown.expenses) valid = valid && value >= 0;
        const auto& flow = ledger.flow;
        for (auto value : {ledger.fundsBefore, ledger.fundsAfter, ledger.debtBefore,
                          flow.revenue, flow.expenses, flow.shortfall,
                          flow.debtIssued, flow.debtRepaid, flow.interestCharged,
                          flow.outstandingDebt}) valid = valid && value >= 0;
        valid = valid && std::accumulate(ledger.breakdown.revenue.begin(),
          ledger.breakdown.revenue.end(), 0.L)+ledger.breakdown.tourismFees == flow.revenue;
        valid = valid && std::accumulate(ledger.breakdown.expenses.begin(),
          ledger.breakdown.expenses.end(), 0.L)+ledger.breakdown.tourismServices == flow.expenses;
        valid = valid && flow.net == ledger.fundsAfter - ledger.fundsBefore;
        valid = valid && flow.shortfall == flow.debtIssued;
        valid = valid && static_cast<long double>(ledger.fundsBefore) + flow.revenue -
          flow.expenses + flow.debtIssued - flow.debtRepaid == ledger.fundsAfter;
        valid = valid && static_cast<long double>(ledger.debtBefore) + flow.debtIssued +
          flow.interestCharged - flow.debtRepaid == flow.outstandingDebt;
        if (!valid) return fail(errorMessage, "gameplay session has inconsistent treasury ledger");
      }
    }
    loaded.paused = root.at("paused").get<bool>();
    loaded.tickIntervalMs = root.at("tickIntervalMs").get<uint32_t>();
    loaded.treasuryRevenue = root.value("treasuryRevenue", int64_t{0});
    loaded.treasuryExpenses = root.value("treasuryExpenses", int64_t{0});
    loaded.treasuryNet = root.value("treasuryNet", int64_t{0});
    loaded.treasuryDebt = root.value("treasuryDebt", int64_t{0});
    loaded.treasuryInterestRemainder = root.value("treasuryInterestRemainder", 0.0);
    loaded.populationTarget = root.value("populationTarget", uint32_t{480});
    loaded.laggedEducationCoverage =
        root.value("laggedEducationCoverage", 0.0f);
    if (root.contains("migrationConditions")) {
      const auto &c = root.at("migrationConditions");
      loaded.migrationConditions = {c.at("congestion").get<float>(),
                                    c.at("pollution").get<float>(),
                                    c.at("serviceSatisfaction").get<float>(),
                                    c.at("crimeRate").get<float>(),
                                    c.at("illnessRate").get<float>(),
                                    c.at("utilityAvailability").get<float>()};
    }
    if (root.contains("populationChange")) {
      const auto &c = root.at("populationChange");
      loaded.populationChange = {
          c.at("arrivals").get<uint32_t>(), c.at("departures").get<uint32_t>(),
          c.at("deaths").get<uint32_t>(), c.at("net").get<int64_t>(),
          c.at("desirability").get<float>()};
      const auto &change = loaded.populationChange;
      if (change.net != static_cast<int64_t>(change.arrivals) -
                            change.departures - change.deaths)
        return fail(errorMessage,
                    "gameplay session has inconsistent population change");
    }
    const auto &c = loaded.migrationConditions;
    for (float value :
         {loaded.laggedEducationCoverage, c.congestion, c.pollution,
          c.serviceSatisfaction, c.crimeRate, c.illnessRate,
          c.utilityAvailability, loaded.populationChange.desirability}) {
      if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
        return fail(errorMessage,
                    "gameplay session has invalid migration conditions");
    }
    if (root.contains("growthModifiers")) {
      for (const auto &item : root.at("growthModifiers")) {
        GrowthChanceModifier modifier;
        modifier.minCorner = {item.at("x1").get<int>(),
                              item.at("y1").get<int>()};
        modifier.maxCorner = {item.at("x2").get<int>(),
                              item.at("y2").get<int>()};
        modifier.multiplier = item.at("multiplier").get<float>();
        if (!map.isValid(modifier.minCorner) ||
            !map.isValid(modifier.maxCorner) ||
            modifier.minCorner.x > modifier.maxCorner.x ||
            modifier.minCorner.y > modifier.maxCorner.y ||
            !std::isfinite(modifier.multiplier) || modifier.multiplier < 0.0f)
          return fail(errorMessage,
                      "gameplay session has invalid growth modifiers");
        loaded.growthModifiers.push_back(modifier);
      }
    }
    loaded.fractionalDeaths = root.value("fractionalDeaths", 0.0);
    loaded.awaitingDisposition = root.value("awaitingDisposition", uint32_t{0});
    loaded.autonomousGrowth = root.value("autonomousGrowth", false);
    loaded.autonomousExtent = root.value("autonomousExtent", 0);
    loaded.emptyZonedCount = root.value("emptyZonedCount", int64_t{0});
    if (loaded.funds < 0 || loaded.tickIntervalMs < 16 || loaded.tickIntervalMs > 5000) {
      return fail(errorMessage, "gameplay session has invalid funds or speed");
    }
    if (loaded.treasuryDebt < 0 || (!std::isfinite(loaded.treasuryInterestRemainder) || loaded.treasuryInterestRemainder < 0.0 || loaded.treasuryInterestRemainder >= 1.0)) {
      return fail(errorMessage, "gameplay session has invalid treasury debt");
    }
    if (loaded.autonomousExtent < 0 || loaded.emptyZonedCount < 0) {
      return fail(errorMessage, "gameplay session has invalid autonomousExtent or emptyZonedCount");
    }
    const json& demand = root.at("demand");
    loaded.demand = {
      demand.at("residential").get<float>(), demand.at("commercial").get<float>(),
      demand.at("industrial").get<float>(), demand.at("office").get<float>()
    };

    for (const json& item : root.at("facilities")) {
      const int type = item.at("type").get<int>();
      const Coord position{item.at("x").get<int>(), item.at("y").get<int>()};
      if (!validServiceType(type) || !map.isValid(position)) {
        return fail(errorMessage, "gameplay session contains an invalid facility");
      }
      ServiceFacility facility;
      facility.type = static_cast<ServiceType>(type);
      facility.position = position;
      facility.maxTravelDistance = item.at("maxTravelDistance").get<int>();
      facility.quality = item.at("quality").get<float>();
      facility.powerSource = static_cast<PowerSourceType>(item.value("powerSource", 0));
      facility.powerCapacityMW = item.value("powerCapacityMW", 100.0f);
      facility.emissionsKgPerMWh = item.value("emissionsKgPerMWh", 400.0f);
      facility.waterSupplyUnits = item.value("waterSupplyUnits", 100.0f);
      loaded.facilities.push_back(facility);
    }

    if (root.contains("transitRoutes")) {
      if (!parseTransitRoutes(root.at("transitRoutes"), map, loaded.transitRoutes, errorMessage)) {
        return false;
      }
    }

    if (root.contains("regionalLogisticsEnabled")) {
      if (!root.at("regionalLogisticsEnabled").is_boolean())
        throw std::runtime_error("Invalid regional logistics mode");
      loaded.regionalLogisticsEnabled=root.at("regionalLogisticsEnabled").get<bool>();
    }
    if (root.contains("regionalConnections"))
      loaded.regionalConnections=parseRegionalConnections(root.at("regionalConnections"),map);
    if (root.contains("nextRegionalConnectionId")) {
      const auto& value=root.at("nextRegionalConnectionId");
      if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>()<1) ||
          value.get<uint64_t>()<1 || value.get<uint64_t>()>static_cast<uint64_t>(UINT32_MAX)+1)
        throw std::runtime_error("Invalid next regional connection ID");
      loaded.nextRegionalConnectionId=value.get<uint64_t>();
    }
    for (const auto& c:loaded.regionalConnections)
      loaded.nextRegionalConnectionId=std::max(loaded.nextRegionalConnectionId,static_cast<uint64_t>(c.id)+1);
    if (root.contains("regionalSettlement"))
      loaded.regionalSettlement=parseRegionalSettlement(root.at("regionalSettlement"),loaded.tick);

    if (root.contains("districts")) {
      if (!parseDistricts(root.at("districts"), map, loaded.districts, errorMessage)) {
        return false;
      }
    }

    const std::string cityPath = snapshotPath(root, sessionPath);
    SnapshotLoadDiagnostics diagnostics;
    CitySnapshot snapshot;
    if (!SaveLoadSystem::loadSnapshotFromFile(cityPath,snapshot,&diagnostics)) {
      return fail(errorMessage, diagnostics.errorMessage.empty()
        ? "unable to load core city snapshot" : diagnostics.errorMessage);
    }
    loaded.tourism.state.tick=loaded.tick;
    if (root.contains("tourism")) loaded.tourism=parseTourism(root.at("tourism"),loaded.tick,snapshot,&loaded.transitReport);
    validateTourismAccounting(loaded.tourism,loaded.treasuryLedger);
    std::vector<Coord> civicSites;
    for (const auto& facility:loaded.facilities) civicSites.push_back(facility.position);
    if (!validateHotelCivicSites(snapshot,civicSites,errorMessage) ||
        !validateRegionalReservations(snapshot,loaded.regionalConnections,civicSites,errorMessage)) return false;
    if (!SaveLoadSystem::applySnapshot(snapshot,map,roads,store,population,errorMessage)) return false;
    rebuildRegionalReservations(map,loaded.regionalConnections);
    session = std::move(loaded);
    return true;
  } catch (const std::exception& e) {
    return fail(errorMessage, std::string("invalid gameplay session: ") + e.what());
  }
}

PlayableCityTickState GameplaySessionSystem::advance(
  CityMap& map, RoadNetwork& roads, EntityStore& store,
  PopulationStore& population, GameplaySessionState& session
) {
  PlayableCityTickState state;
  state.tick = session.tick;
  state.budgetPolicy = session.budgetPolicy;
  state.treasuryLedger = session.treasuryLedger;
  state.history = session.history;
  state.alerts = session.alerts;
  state.regionalConnections = session.regionalConnections;
  state.regionalSettlement = session.regionalSettlement;
  state.tourism = session.tourism;
  state.regionalLogisticsEnabled = session.regionalLogisticsEnabled;
  state.transitReport = session.transitReport;
  state.populationTarget = session.populationTarget;
  state.migrationConditions = session.migrationConditions;
  state.populationChange = session.populationChange;
  state.demand = session.demand;
  state.treasuryRevenue = session.treasuryRevenue;
  state.treasuryExpenses = session.treasuryExpenses;
  state.treasuryNet = session.treasuryNet;
  state.treasuryDebt = session.treasuryDebt;
  state.treasuryInterestRemainder = session.treasuryInterestRemainder;
  state.laggedEducationCoverage = session.laggedEducationCoverage;
  state.transitRoutes = session.transitRoutes;
  state.disasters = session.disasters;
  state.deathcareState = {session.fractionalDeaths, session.awaitingDisposition};
  state.districtGrowthModifiers = session.growthModifiers;
  DistrictSystem districts;
  for (const auto& record : session.districts) {
    const auto id = districts.createDistrict(record.name.empty() ? "DISTRICT" : record.name,
                                             {record.x1, record.y1}, {record.x2, record.y2});
    districts.setDistrictArchetype(id, static_cast<DistrictArchetype>(record.archetype));
    districts.setDistrictServiceBudgetCap(id, record.serviceBudgetCap);
  }
  city_sim::ConstructionState construction;
  construction.extent = session.autonomousExtent;
  construction.emptyZonedCount = session.emptyZonedCount;
  if (session.autonomousGrowth) {
    state.demand = (store.getBuildingCount() > 0 || population.getTotalPopulation() > 0)
      ? CitySimulator::evaluateDemand(store, population)
      : Zoning::calculateDemand(1000u + state.tick * 17u);
    static thread_local ThreadPool pool(std::max(1u,
      std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() - 1 : 2u));
    city_sim::ConstructionOptions constructionOptions;
    constructionOptions.placeFacilities = true;
    constructionOptions.includeUtilities = true;
    constructionOptions.includeWasteDeathcare = true;
    constructionOptions.districts = &districts;
    city_sim::expandConstruction(map, roads, store, population, session.facilities,
                                 state.transitRoutes, construction, pool,
                                 state.demand, constructionOptions);
  }
  PlayableCityTickOptions options;
  options.baseSeed = 1000u + session.simulationSeed - 98765u;
  options.enableDisasters = session.enableDisasters;
  options.districts = &districts;
  playableCityTick(map, roads, store, population, session.facilities,
                   state, session.funds, options);
  if (session.autonomousGrowth)
    city_sim::applyEmptyZonedDelta(construction,
      static_cast<int64_t>(state.buildingsDemolished) - state.buildingsSpawned);
  session.autonomousExtent = construction.extent;
  session.emptyZonedCount = construction.emptyZonedCount;
  session.tick = state.tick;
  session.budgetPolicy = state.budgetPolicy;
  session.treasuryLedger = state.treasuryLedger;
  session.history = state.history;
  session.alerts = state.alerts;
  session.regionalConnections = state.regionalConnections;
  session.regionalSettlement = state.regionalSettlement;
  session.tourism = state.tourism;
  session.regionalLogisticsEnabled = state.regionalLogisticsEnabled;
  session.transitReport = state.transitReport;
  session.populationTarget = state.populationTarget;
  session.migrationConditions = state.migrationConditions;
  session.populationChange = state.populationChange;
  session.demand = state.demand;
  session.treasuryRevenue = state.treasuryRevenue;
  session.treasuryExpenses = state.treasuryExpenses;
  session.treasuryNet = state.treasuryNet;
  session.treasuryDebt = state.treasuryDebt;
  session.treasuryInterestRemainder = state.treasuryInterestRemainder;
  session.laggedEducationCoverage = state.laggedEducationCoverage;
  session.transitRoutes = state.transitRoutes;
  session.disasters = state.disasters;
  session.fractionalDeaths = state.deathcareState.fractionalDeaths;
  session.awaitingDisposition = state.deathcareState.awaitingDisposition;
  session.growthModifiers = state.districtGrowthModifiers;
  return state;
}
