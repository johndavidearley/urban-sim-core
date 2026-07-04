#include "FireSystem.hpp"

#include <algorithm>
#include <unordered_set>

#include "src/core/Random.hpp"

namespace {

struct Vec2HashLocal {
  size_t operator()(const Coord& c) const {
    return std::hash<int>()(c.x) * 73856093 ^ std::hash<int>()(c.y) * 19349663;
  }
};

void destroyBuildingAt(CityMap& map, EntityStore& store, Coord pos) {
  Tile& tile = map.getTile(pos);
  if (tile.buildingId != EntityIdUtils::NullEntity) {
    store.removeBuilding(tile.buildingId);
    tile.buildingId = EntityIdUtils::NullEntity;
  }
}

} // namespace

FireSummary FireSystem::step(
  CityMap& map,
  EntityStore& store,
  std::vector<BurningTile>& burning,
  float fireCoverage,
  uint32_t seed,
  const FireParams& params
) {
  FireSummary summary;
  const float coverage = std::clamp(fireCoverage, 0.0f, 1.0f);
  DeterministicRandom rng(seed);

  // Fixed order for determinism: burning tiles sorted by position.
  std::vector<BurningTile> sortedBurning = burning;
  std::sort(sortedBurning.begin(), sortedBurning.end(), [](const BurningTile& a, const BurningTile& b) {
    if (a.position.y != b.position.y) return a.position.y < b.position.y;
    return a.position.x < b.position.x;
  });

  std::unordered_set<Coord, Vec2HashLocal> alreadyBurning;
  for (const BurningTile& bt : sortedBurning) {
    alreadyBurning.insert(bt.position);
  }

  // Phase 1: spread from existing fires to adjacent occupied, non-burning tiles.
  const float spreadChance = params.spreadChance * (1.0f - coverage * params.coverageSpreadReduction);
  std::vector<Coord> ignitedThisStep;
  std::unordered_set<Coord, Vec2HashLocal> ignitedSet;
  for (const BurningTile& bt : sortedBurning) {
    const Coord neighbors[4] = {
      {bt.position.x + 1, bt.position.y}, {bt.position.x - 1, bt.position.y},
      {bt.position.x, bt.position.y + 1}, {bt.position.x, bt.position.y - 1}
    };
    for (const Coord& n : neighbors) {
      if (!map.isValid(n)) continue;
      if (alreadyBurning.count(n) != 0 || ignitedSet.count(n) != 0) continue;
      const Tile& nt = map.getTile(n);
      if (nt.buildingId == EntityIdUtils::NullEntity) continue;
      if (rng.chance(std::max(0.0f, spreadChance))) {
        ignitedThisStep.push_back(n);
        ignitedSet.insert(n);
      }
    }
  }

  // Phase 2: independent random ignitions, weighted by building type and
  // local pollution. Buildings iterated in ID order for determinism.
  std::vector<const Building*> buildings;
  buildings.reserve(store.getBuildings().size());
  for (const auto& [id, b] : store.getBuildings()) {
    (void)id;
    buildings.push_back(&b);
  }
  std::sort(buildings.begin(), buildings.end(), [](const Building* a, const Building* b) { return a->id < b->id; });

  for (const Building* b : buildings) {
    if (alreadyBurning.count(b->position) != 0 || ignitedSet.count(b->position) != 0) continue;
    float risk = params.baseIgnitionChance;
    if (b->type == BuildingType::Industrial) {
      risk *= params.industrialRiskMultiplier;
    }
    const float pollution = std::clamp(map.pollution(b->position), 0.0f, 1.0f);
    risk *= (1.0f + pollution * params.pollutionRiskMultiplier);
    risk *= (1.0f - coverage * params.coverageIgnitionReduction);
    if (rng.chance(std::max(0.0f, risk))) {
      ignitedThisStep.push_back(b->position);
      ignitedSet.insert(b->position);
    }
  }

  // Phase 3: apply ignitions - the structure is destroyed immediately, the
  // tile keeps burning (posing further spread risk) for a coverage-shortened
  // duration.
  const int duration = std::max(1, static_cast<int>(
    std::lround(static_cast<double>(params.baseDuration) * (1.0 - static_cast<double>(coverage) * params.coverageDurationReduction))));
  for (const Coord& c : ignitedThisStep) {
    destroyBuildingAt(map, store, c);
  }
  summary.newIgnitions = static_cast<uint32_t>(ignitedThisStep.size());
  summary.buildingsDestroyed = summary.newIgnitions;

  // Phase 4: age existing fires by one tick; drop ones that burn out.
  std::vector<BurningTile> next;
  next.reserve(sortedBurning.size() + ignitedThisStep.size());
  for (const BurningTile& bt : sortedBurning) {
    const int remaining = bt.ticksRemaining - 1;
    if (remaining > 0) {
      next.push_back({bt.position, remaining});
    }
  }
  for (const Coord& c : ignitedThisStep) {
    next.push_back({c, duration});
  }

  burning = std::move(next);
  summary.activeFires = static_cast<uint32_t>(burning.size());
  return summary;
}
