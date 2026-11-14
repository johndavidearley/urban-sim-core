#pragma once
#include <cstdint>

namespace visualizer {
// One tick per frame, retaining phase so small render delays do not accumulate.
// Paused/modal wall time is discarded rather than replayed on resume.
inline bool realtimeTickDue(uint32_t now, uint32_t interval, uint32_t& last, bool paused) {
  if (paused || interval == 0) { last = now; return false; }
  if (static_cast<uint32_t>(now-last) < interval) return false;
  last += interval;
  return true;
}
} // namespace visualizer
