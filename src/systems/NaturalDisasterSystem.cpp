#include "NaturalDisasterSystem.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "src/core/Random.hpp"

namespace {

std::vector<const Building*> sortedBuildings(const EntityStore& store) {
  std::vector<const Building*> buildings;
  buildings.reserve(store.getBuildings().size());
  for (const auto& [id, b] : store.getBuildings()) {
    (void)id;
    buildings.push_back(&b);
  }
  std::sort(buildings.begin(), buildings.end(), [](const Building* a, const Building* b) { return a->id < b->id; });
  return buildings;
}

bool isNearWater(const CityMap& map, Coord position, int proximity) {
  const glm::ivec2 dims = map.getDimensions();
  const int x0 = std::max(0, position.x - proximity);
  const int x1 = std::min(dims.x - 1, position.x + proximity);
  const int y0 = std::max(0, position.y - proximity);
  const int y1 = std::min(dims.y - 1, position.y + proximity);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (map.getTile({x, y}).type == 2) {
        return true;
      }
    }
  }
  return false;
}

// Destroys buildings within `radius` of `epicenter`, chance falling off
// linearly from `destructionChanceAtEpicenter` (at distance 0) to 0 (at the
// radius edge). Buildings are considered in a fixed ID-sorted order for
// determinism. Returns the number destroyed.
uint32_t destroyWithinRadius(
  CityMap& map,
  EntityStore& store,
  DeterministicRandom& rng,
  Coord epicenter,
  int radius,
  float destructionChanceAtEpicenter
) {
  std::vector<EntityId> toDestroy;
  for (const Building* b : sortedBuildings(store)) {
    const float dx = static_cast<float>(b->position.x - epicenter.x);
    const float dy = static_cast<float>(b->position.y - epicenter.y);
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > static_cast<float>(radius)) continue;
    const float falloff = 1.0f - dist / static_cast<float>(radius);
    if (rng.chance(std::max(0.0f, destructionChanceAtEpicenter * falloff))) {
      toDestroy.push_back(b->id);
    }
  }

  uint32_t destroyed = 0;
  for (EntityId id : toDestroy) {
    Building* b = store.getBuilding(id);
    if (b == nullptr) continue;
    Tile& tile = map.getTile(b->position);
    if (tile.buildingId == static_cast<uint32_t>(id)) {
      tile.buildingId = EntityIdUtils::NullEntity;
    }
    store.removeBuilding(id);
    ++destroyed;
  }
  return destroyed;
}

} // namespace

DisasterSummary NaturalDisasterSystem::step(
  CityMap& map,
  EntityStore& store,
  uint32_t seed,
  const DisasterParams& params
) {
  DisasterSummary summary;
  DeterministicRandom rng(seed);

  // Earthquake: uniform citywide risk. The epicenter is anchored at a random
  // existing building (guarantees the event actually threatens development
  // rather than striking empty land) when any exist.
  {
    const std::vector<const Building*> buildings = sortedBuildings(store);
    if (!buildings.empty() && rng.chance(params.earthquakeChancePerTick)) {
      const uint32_t idx = rng.integer(0, static_cast<uint32_t>(buildings.size() - 1));
      const Coord epicenter = buildings[idx]->position;
      summary.earthquakeOccurred = true;
      summary.buildingsDestroyed +=
        destroyWithinRadius(map, store, rng, epicenter, std::max(1, params.earthquakeRadius), params.earthquakeDestructionChance);
    }
  }

  // Flood: same roll mechanics, but restricted to buildings within
  // floodProximity of a water tile - a flood can't strike inland development.
  {
    std::vector<const Building*> nearWater;
    for (const Building* b : sortedBuildings(store)) {
      if (isNearWater(map, b->position, params.floodProximity)) {
        nearWater.push_back(b);
      }
    }
    if (!nearWater.empty() && rng.chance(params.floodChancePerTick)) {
      const uint32_t idx = rng.integer(0, static_cast<uint32_t>(nearWater.size() - 1));
      const Coord epicenter = nearWater[idx]->position;
      summary.floodOccurred = true;
      summary.buildingsDestroyed +=
        destroyWithinRadius(map, store, rng, epicenter, std::max(1, params.floodRadius), params.floodDestructionChance);
    }
  }

  return summary;
}
