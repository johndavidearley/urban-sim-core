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

float FireSystem::responseAt(const RoadNetwork& roads, const ServiceCoverageCache& cache,
    Coord position, float fallbackCoverage) {
  const bool hasStation = std::any_of(cache.entries.begin(), cache.entries.end(),
    [](const auto& entry) { return entry.type == ServiceType::Fire; });
  if (!hasStation) return std::clamp(fallbackCoverage, 0.f, 1.f);
  Coord anchor;
  return roads.resolveRoadAnchor(position, anchor)
    ? ServiceSystem::coverageStrength(cache, ServiceType::Fire, anchor) *
      ServiceSystem::departmentFactorAt(cache, ServiceType::Fire, position) : 0.f;
}

FireSummary FireSystem::step(
  CityMap& map,
  EntityStore& store,
  std::vector<BurningTile>& burning,
  float fireCoverage,
  uint32_t seed,
  const FireParams& params,
  const RoadNetwork* roads,
  const ServiceCoverageCache* cache
) {
  FireSummary summary;
  const float coverage = std::clamp(fireCoverage, 0.0f, 1.0f);
  const auto response = [&](Coord position) {
    return roads && cache ? responseAt(*roads, *cache, position, coverage) : coverage;
  };
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

  // Phase 1: local response may put each fire out before it spreads.
  std::vector<BurningTile> active;
  active.reserve(sortedBurning.size());
  for (const BurningTile& bt : sortedBurning) {
    const bool standing = map.getTile(bt.position).buildingId != EntityIdUtils::NullEntity;
    if (standing && rng.chance(std::clamp(response(bt.position) * params.extinguishChance, 0.f, 1.f))) {
      ++summary.extinguished;
      continue;
    }
    active.push_back(bt);
  }

  // Phase 2: spread from fires still burning to adjacent occupied, non-burning tiles.
  std::vector<Coord> ignitedThisStep;
  std::unordered_set<Coord, Vec2HashLocal> ignitedSet;
  for (const BurningTile& bt : active) {
    const Coord neighbors[4] = {
      {bt.position.x + 1, bt.position.y}, {bt.position.x - 1, bt.position.y},
      {bt.position.x, bt.position.y + 1}, {bt.position.x, bt.position.y - 1}
    };
    for (const Coord& n : neighbors) {
      if (!map.isValid(n)) continue;
      if (alreadyBurning.count(n) != 0 || ignitedSet.count(n) != 0) continue;
      const Tile& nt = map.getTile(n);
      if (nt.buildingId == EntityIdUtils::NullEntity) continue;
      const float spreadChance = params.spreadChance * (1.f - response(n) * params.coverageSpreadReduction);
      if (rng.chance(std::max(0.0f, spreadChance))) {
        ignitedThisStep.push_back(n);
        ignitedSet.insert(n);
      }
    }
  }

  // Phase 3: independent random ignitions, weighted by building type and
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
    risk *= (1.0f - response(b->position) * params.coverageIgnitionReduction);
    if (rng.chance(std::max(0.0f, risk))) {
      ignitedThisStep.push_back(b->position);
      ignitedSet.insert(b->position);
    }
  }
  summary.newIgnitions = static_cast<uint32_t>(ignitedThisStep.size());

  // Phase 4: unfought fires burn down when their countdown ends. A lot whose
  // building was already removed (bulldozed, or a pre-M47 save) just clears.
  std::vector<BurningTile> next;
  next.reserve(active.size() + ignitedThisStep.size());
  for (const BurningTile& bt : active) {
    const int remaining = bt.ticksRemaining - 1;
    if (remaining > 0) {
      next.push_back({bt.position, remaining});
    } else if (map.getTile(bt.position).buildingId != EntityIdUtils::NullEntity) {
      destroyBuildingAt(map, store, bt.position);
      ++summary.buildingsDestroyed;
    }
  }
  for (const Coord& c : ignitedThisStep) {
    next.push_back({c, std::max(1, params.baseDuration)});
  }

  burning = std::move(next);
  summary.activeFires = static_cast<uint32_t>(burning.size());
  return summary;
}
