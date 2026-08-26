#pragma once

#include <vector>

#include "src/entities/EntityStore.hpp"

// Pointer views over EntityStore's maintained type indices. Lists are already
// sorted by EntityId (stable across hash-map iteration), so no per-call sort.
struct BuildingPartitions {
  std::vector<const Building*> all;
  std::vector<const Building*> residential;
  // Commercial + Industrial + Office (commute destinations / job access).
  std::vector<const Building*> jobs;
  std::vector<const Building*> commercial;
  std::vector<const Building*> industrial;
  std::vector<const Building*> office;

  // sortById is retained for call-site compatibility; indices are always
  // ID-sorted, so the flag is ignored.
  static BuildingPartitions fromStore(const EntityStore& store, bool sortById = true) {
    (void)sortById;
    BuildingPartitions p;

    auto resolve = [&store](const std::vector<EntityId>& ids,
                            std::vector<const Building*>& out) {
      out.reserve(ids.size());
      for (EntityId id : ids) {
        if (const Building* b = store.getBuilding(id)) {
          out.push_back(b);
        }
      }
    };

    resolve(store.idsByBuildingType(BuildingType::Residential), p.residential);
    resolve(store.idsByBuildingType(BuildingType::Commercial), p.commercial);
    resolve(store.idsByBuildingType(BuildingType::Industrial), p.industrial);
    resolve(store.idsByBuildingType(BuildingType::Office), p.office);
    resolve(store.jobIds(), p.jobs);

    p.all.reserve(store.getBuildingCount());
    p.all.insert(p.all.end(), p.residential.begin(), p.residential.end());
    p.all.insert(p.all.end(), p.jobs.begin(), p.jobs.end());
    return p;
  }
};
