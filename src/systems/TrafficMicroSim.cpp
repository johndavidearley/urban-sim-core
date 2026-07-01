#include "src/systems/TrafficMicroSim.hpp"

#include <algorithm>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "src/networks/Pathfinding.hpp"

namespace {

// Vehicles occupy the edge between route[segment] and route[segment+1].
RoadNetwork::EdgeKey edgeOf(const Vehicle& v) {
  return RoadNetwork::EdgeKey{v.route[v.segment], v.route[v.segment + 1]};
}

// A signalized junction alternates green between the horizontal (E-W) and
// vertical (N-S) axes. Phases are offset by the node's coordinates so adjacent
// intersections are not all red at once (a rough green-wave / checkerboard).
bool signalGreen(glm::ivec2 node, bool horizontalMove, int step, int period) {
  const int phase = ((step / std::max(1, period)) + node.x + node.y) % 2;
  const bool horizontalGreen = (phase == 0);
  return horizontalMove ? horizontalGreen : !horizontalGreen;
}

// RoadNetwork pre-creates a node for every tile, so hasNode() is always true;
// an actual road node is one with road adjacency. Buildings sit next to roads,
// not on them, so resolve the adjacent road node to use as a routing endpoint.
bool isRoadNode(const RoadNetwork& roads, glm::ivec2 pos) {
  const RoadNetwork::Node* node = roads.getNode(pos);
  return node != nullptr && !node->adjacent.empty();
}

bool roadAnchor(const RoadNetwork& roads, glm::ivec2 pos, glm::ivec2& out) {
  if (isRoadNode(roads, pos)) {
    out = pos;
    return true;
  }
  const glm::ivec2 neighbors[4] = {
    {pos.x + 1, pos.y}, {pos.x - 1, pos.y}, {pos.x, pos.y + 1}, {pos.x, pos.y - 1}
  };
  for (const glm::ivec2& n : neighbors) {
    if (isRoadNode(roads, n)) {
      out = n;
      return true;
    }
  }
  return false;
}

// Deterministic, id-ordered home/job sampling (matches TrafficSystem's ordering
// discipline so runs are reproducible across platforms).
struct CommuteSpec {
  glm::ivec2 home;
  glm::ivec2 work;
  uint32_t people;
};

std::vector<CommuteSpec> collectCommutes(
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  uint32_t seed,
  uint32_t& outCommuters
) {
  std::vector<CommuteSpec> specs;
  outCommuters = 0;

  std::vector<const Building*> homes;
  std::vector<const Building*> jobs;
  for (const auto& [id, b] : store.getBuildings()) {
    (void)id;
    if (b.type == BuildingType::Residential) homes.push_back(&b);
    else if (b.type == BuildingType::Commercial || b.type == BuildingType::Industrial) jobs.push_back(&b);
  }
  if (homes.empty() || jobs.empty()) return specs;

  auto byId = [](const Building* a, const Building* b) { return a->id < b->id; };
  std::sort(homes.begin(), homes.end(), byId);
  std::sort(jobs.begin(), jobs.end(), byId);

  std::vector<const PopulationGroup*> groups;
  for (const auto& [id, g] : population.getGroups()) {
    (void)id;
    groups.push_back(&g);
  }
  std::sort(groups.begin(), groups.end(),
    [](const PopulationGroup* a, const PopulationGroup* b) { return a->id < b->id; });

  std::mt19937 rng(seed);
  std::uniform_int_distribution<size_t> homeDist(0, homes.size() - 1);
  std::uniform_int_distribution<size_t> jobDist(0, jobs.size() - 1);

  for (const PopulationGroup* g : groups) {
    if (g->employed == 0) continue;
    outCommuters += g->employed;
    const uint32_t perVehicle = std::max(1u, g->employed / 10u);
    const uint32_t count = (g->employed + perVehicle - 1) / perVehicle;
    for (uint32_t c = 0; c < count; ++c) {
      const Building* home = homes[homeDist(rng)];
      const Building* work = jobs[jobDist(rng)];
      glm::ivec2 homeAnchor;
      glm::ivec2 workAnchor;
      if (!roadAnchor(roads, home->position, homeAnchor) ||
          !roadAnchor(roads, work->position, workAnchor)) {
        continue;
      }
      const uint32_t people = std::min(perVehicle, g->employed - c * perVehicle);
      specs.push_back({homeAnchor, workAnchor, people});
    }
  }
  return specs;
}

} // namespace

MicroTrafficSummary TrafficMicroSim::simulate(
  const EntityStore& store,
  const PopulationStore& population,
  const RoadNetwork& roads,
  uint32_t seed,
  const Options& options
) {
  MicroTrafficSummary summary;

  uint32_t commuters = 0;
  const std::vector<CommuteSpec> specs = collectCommutes(store, population, roads, seed, commuters);
  summary.commutingPopulation = commuters;
  if (specs.empty()) {
    return summary;
  }

  // Route each commute once (free-flow shortest path); spawn a vehicle per batch.
  std::vector<Vehicle> vehicles;
  vehicles.reserve(specs.size());
  for (const CommuteSpec& spec : specs) {
    Pathfinding::Path path = Pathfinding::findShortestPath(roads, spec.home, spec.work);
    if (!path.found || path.waypoints.size() < 2) continue;

    Vehicle v;
    v.id = static_cast<uint32_t>(vehicles.size());
    v.type = VehicleType::Car;
    v.route = std::move(path.waypoints);
    v.people = spec.people;
    vehicles.push_back(std::move(v));
  }

  summary.vehicles = static_cast<uint32_t>(vehicles.size());
  if (vehicles.empty()) {
    return summary;
  }

  // Intersections are road nodes with 3+ connections; each gets a signal.
  std::unordered_set<glm::ivec2, Vec2Hash> intersections;
  if (options.enableSignals) {
    for (const glm::ivec2& tile : roads.getAllRoadTiles()) {
      const RoadNetwork::Node* node = roads.getNode(tile);
      if (node != nullptr && node->adjacent.size() >= 3) {
        intersections.insert(tile);
      }
    }
  }
  summary.signalizedIntersections = static_cast<uint32_t>(intersections.size());

  std::unordered_map<RoadNetwork::EdgeKey, int, RoadNetwork::EdgeKeyHash> occupancy;
  occupancy.reserve(vehicles.size() * 2);

  double occupancyAccum = 0.0;
  uint32_t occupancySamples = 0;
  uint32_t arrivedCount = 0;
  uint64_t totalWaitSteps = 0;
  int step = 0;

  for (; step < options.maxSteps && arrivedCount < vehicles.size(); ++step) {
    // Snapshot edge occupancy so every vehicle this step sees the same field
    // (keeps movement independent of processing order -> deterministic).
    occupancy.clear();
    for (const Vehicle& v : vehicles) {
      if (v.arrived) continue;
      occupancy[edgeOf(v)] += 1;
    }

    float peakThisStep = 0.0f;
    for (const auto& [key, count] : occupancy) {
      (void)key;
      peakThisStep = std::max(peakThisStep, static_cast<float>(count));
      occupancyAccum += count;
      ++occupancySamples;
    }
    summary.peakEdgeOccupancy = std::max(summary.peakEdgeOccupancy, peakThisStep);

    const uint32_t currentStep = static_cast<uint32_t>(step);
    for (Vehicle& v : vehicles) {
      if (v.arrived) continue;

      const int sharing = occupancy[edgeOf(v)];
      float speed = options.baseSpeed;
      if (v.type != VehicleType::Emergency && sharing > 1) {
        speed = options.baseSpeed / (1.0f + options.congestionSlowing * static_cast<float>(sharing - 1));
      }
      speed = std::max(options.minSpeed, speed);

      v.progress += speed;
      while (v.progress >= 1.0f && !v.arrived) {
        // Vehicle reaches node route[segment+1].
        if (v.segment + 1 >= v.route.size() - 1) {
          v.arrived = true;
          v.arriveStep = currentStep + 1;
          ++arrivedCount;
          v.progress = 0.0f;
          break;
        }

        // To continue it must enter edge (route[segment+1] -> route[segment+2]).
        // Stop on red at a signalized intersection, queueing on the approach edge.
        const glm::ivec2 node = v.route[v.segment + 1];
        if (!intersections.empty() && intersections.count(node) != 0) {
          const glm::ivec2 next = v.route[v.segment + 2];
          const bool horizontalMove = (next.x != node.x);
          if (!signalGreen(node, horizontalMove, static_cast<int>(currentStep), options.signalPeriod)) {
            v.progress = 1.0f;  // wait at the node, still occupying the approach edge
            ++totalWaitSteps;
            break;
          }
        }

        v.progress -= 1.0f;
        v.segment += 1;
      }
    }
  }

  summary.stepsSimulated = static_cast<uint32_t>(step);
  summary.arrived = arrivedCount;
  summary.averageEdgeOccupancy = occupancySamples > 0
    ? static_cast<float>(occupancyAccum / occupancySamples) : 0.0f;
  summary.averageSignalWaitSteps = !vehicles.empty()
    ? static_cast<float>(static_cast<double>(totalWaitSteps) / vehicles.size()) : 0.0f;

  uint64_t tripStepsTotal = 0;
  for (const Vehicle& v : vehicles) {
    if (v.arrived) {
      tripStepsTotal += v.arriveStep;
    }
  }
  summary.averageTripSteps = arrivedCount > 0
    ? static_cast<float>(static_cast<double>(tripStepsTotal) / arrivedCount) : 0.0f;

  return summary;
}
