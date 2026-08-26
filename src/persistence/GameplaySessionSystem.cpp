#include "src/persistence/GameplaySessionSystem.hpp"

#include <exception>
#include <fstream>

#include <nlohmann/json.hpp>

#include "src/persistence/SaveLoadSystem.hpp"

using nlohmann::json;

namespace {
constexpr int kGameplaySessionVersion = 1;

std::string cityPathFor(const std::string& sessionPath) {
  return sessionPath + ".city.json";
}

bool fail(std::string* errorMessage, const std::string& message) {
  if (errorMessage != nullptr) {
    *errorMessage = message;
  }
  return false;
}

bool validServiceType(int value) {
  return value >= static_cast<int>(ServiceType::Fire)
      && value <= static_cast<int>(ServiceType::Crematorium);
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
    const std::string cityPath = root.value("citySnapshot", cityPathFor(sessionPath));
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
      {"emissionsKgPerMWh", facility.emissionsKgPerMWh}
    });
  }

  const json root = {
    {"version", kGameplaySessionVersion},
    {"citySnapshot", cityPathFor(sessionPath)},
    {"funds", session.funds},
    {"tick", session.tick},
    {"paused", session.paused},
    {"tickIntervalMs", session.tickIntervalMs},
    {"treasuryRevenue", session.treasuryRevenue},
    {"treasuryExpenses", session.treasuryExpenses},
    {"treasuryNet", session.treasuryNet},
    {"populationTarget", session.populationTarget},
    {"fractionalDeaths", session.fractionalDeaths},
    {"awaitingDisposition", session.awaitingDisposition},
    {"autonomousGrowth", session.autonomousGrowth},
    {"autonomousExtent", session.autonomousExtent},
    {"demand", {
      {"residential", session.demand.residential},
      {"commercial", session.demand.commercial},
      {"industrial", session.demand.industrial},
      {"office", session.demand.office}
    }},
    {"facilities", facilities}
  };

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
    loaded.funds = root.at("funds").get<int64_t>();
    loaded.tick = root.at("tick").get<uint32_t>();
    loaded.paused = root.at("paused").get<bool>();
    loaded.tickIntervalMs = root.at("tickIntervalMs").get<uint32_t>();
    loaded.treasuryRevenue = root.value("treasuryRevenue", int64_t{0});
    loaded.treasuryExpenses = root.value("treasuryExpenses", int64_t{0});
    loaded.treasuryNet = root.value("treasuryNet", int64_t{0});
    loaded.populationTarget = root.value("populationTarget", uint32_t{480});
    loaded.fractionalDeaths = root.value("fractionalDeaths", 0.0);
    loaded.awaitingDisposition = root.value("awaitingDisposition", uint32_t{0});
    loaded.autonomousGrowth = root.value("autonomousGrowth", false);
    loaded.autonomousExtent = root.value("autonomousExtent", 0);
    if (loaded.funds < 0 || loaded.tickIntervalMs < 16 || loaded.tickIntervalMs > 5000) {
      return fail(errorMessage, "gameplay session has invalid funds or speed");
    }
    if (loaded.autonomousExtent < 0) {
      return fail(errorMessage, "gameplay session has invalid autonomousExtent");
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
      loaded.facilities.push_back(facility);
    }

    const std::string cityPath = root.value("citySnapshot", cityPathFor(sessionPath));
    SnapshotLoadDiagnostics diagnostics;
    if (!SaveLoadSystem::loadFromFile(
          cityPath, map, roads, store, population, &diagnostics)) {
      return fail(errorMessage, diagnostics.errorMessage.empty()
        ? "unable to load core city snapshot" : diagnostics.errorMessage);
    }
    session = std::move(loaded);
    return true;
  } catch (const std::exception& e) {
    return fail(errorMessage, std::string("invalid gameplay session: ") + e.what());
  }
}
