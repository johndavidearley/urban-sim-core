#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "src/entities/Building.hpp"

// Four building categories in BuildingType order (Residential=1 .. Office=4).
constexpr size_t kBuildingTypeCount = 4;

inline int buildingTypeSlot(BuildingType type) {
  const int slot = static_cast<int>(type) - 1;
  return (slot >= 0 && slot < static_cast<int>(kBuildingTypeCount)) ? slot : -1;
}

inline bool isJobBuildingType(BuildingType type) {
  return type == BuildingType::Commercial
      || type == BuildingType::Industrial
      || type == BuildingType::Office;
}

class EntityStore {
private:
  std::unordered_map<EntityId, Building> buildings;
  uint64_t mutationVersion = 0;

  // ID-sorted indices maintained on create/remove/upsert so callers avoid a
  // full hash-map walk + sort every traffic/transit tick.
  std::array<std::vector<EntityId>, kBuildingTypeCount> idsByType{};
  std::vector<EntityId> jobIds_;  // commercial + industrial + office, sorted by id

  // Aggregate capacity/count for O(1) city capacity summaries.
  std::array<uint32_t, kBuildingTypeCount> countByType_{};
  std::array<uint32_t, kBuildingTypeCount> capacityByType_{};

  static void insertIdSorted(std::vector<EntityId>& ids, EntityId id);
  static bool eraseId(std::vector<EntityId>& ids, EntityId id);
  void indexInsert(const Building& building);
  void indexErase(const Building& building);

public:
  EntityId createBuilding(BuildingType type, Coord position, int capacity);
  bool removeBuilding(EntityId id);
  void clear();
  void upsertBuilding(const Building& building);

  Building* getBuilding(EntityId id);
  const Building* getBuilding(EntityId id) const;
  const std::unordered_map<EntityId, Building>& getBuildings() const;

  size_t getBuildingCount() const;
  uint64_t getMutationVersion() const;

  // Sorted EntityIds for a building type (empty vector if type is invalid).
  const std::vector<EntityId>& idsByBuildingType(BuildingType type) const;
  // Commercial + industrial + office, sorted by id.
  const std::vector<EntityId>& jobIds() const { return jobIds_; }

  uint32_t countOfType(BuildingType type) const;
  uint32_t capacityOfType(BuildingType type) const;
};
