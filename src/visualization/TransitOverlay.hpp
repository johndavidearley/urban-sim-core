#pragma once
#include "src/visualization/VisualizerRender.hpp"
#include "src/networks/Pathfinding.hpp"

namespace visualizer {
struct TransitTileMark {
  Coord neighbor;
  TransitMode mode;
  bool stop;
};
struct TransitMarks : std::unordered_map<Coord, std::vector<TransitTileMark>, Vec2Hash> {};

// Cache geometry by topology and ordered stops; traffic changes do not reroute lines.
class TransitOverlayCache {
 public:
  const TransitMarks& update(const RoadNetwork& roads,
                            const std::vector<TransitRoute>& routes) {
    size_t signature = static_cast<size_t>(roads.getTopologyVersion());
    for (const auto& route : routes) {
      signature = hashCombine(signature, static_cast<size_t>(route.mode));
      signature = hashCombine(signature, route.stops.size());
      for (Coord stop : route.stops) signature = hashCombine(signature, Vec2Hash{}(stop));
    }
    if (valid_ && signature == signature_) return marks_;
    valid_ = true;
    signature_ = signature;
    marks_.clear();
    for (const auto& route : routes) {
      for (Coord stop : route.stops)
        marks_[stop].push_back({stop, route.mode, true});
      for (size_t i = 1; i < route.stops.size(); ++i) {
        const auto path = Pathfinding::findShortestPath(roads, route.stops[i - 1], route.stops[i]);
        if (!path.found) continue; // Never draw across a disconnected neighborhood.
        for (size_t j = 1; j < path.waypoints.size(); ++j) {
          const Coord a = path.waypoints[j - 1], b = path.waypoints[j];
          marks_[a].push_back({b, route.mode, false});
          marks_[b].push_back({a, route.mode, false});
        }
      }
    }
    return marks_;
  }
 private:
  bool valid_ = false;
  size_t signature_ = 0;
  TransitMarks marks_;
};

inline void drawTransitTile(DrawList* renderer, const TransitMarks& marks,
                            Coord coord, Point center, int width, int height,
                            bool isometric) {
  const auto found = marks.find(coord);
  if (found == marks.end()) return;
  // Rail offset keeps shared bus/rail corridors readable.
  for (const auto& mark : found->second) {
    const int offset = mark.mode == TransitMode::Rail ? 2 : -1;
    const RGB color = mark.mode == TransitMode::Rail ? RGB{250, 192, 60} : RGB{35, 210, 240};
    if (mark.stop) continue;
    const Coord delta{mark.neighbor.x - coord.x, mark.neighbor.y - coord.y};
    const int dx = isometric ? (delta.x - delta.y) * width / 4 : delta.x * width / 2;
    const int dy = isometric ? (delta.x + delta.y) * height / 4 : delta.y * height / 2;
    setDrawColor(renderer, color.r, color.g, color.b, 255);
    for (int stroke = 0; stroke < 2; ++stroke)
      drawLine(renderer, center.x, center.y + offset + stroke,
               center.x + dx, center.y + dy + offset + stroke);
  }
  for (const auto& mark : found->second) {
    if (!mark.stop) continue;
    const int offset = mark.mode == TransitMode::Rail ? 2 : -1;
    drawFilledRect(renderer, center.x - 3, center.y + offset - 3, 7, 7, {245, 250, 250});
    drawFilledRect(renderer, center.x - 1, center.y + offset - 1, 3, 3,
                   mark.mode == TransitMode::Rail ? RGB{250, 192, 60} : RGB{35, 210, 240});
  }
}
} // namespace visualizer
