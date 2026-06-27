#pragma once

// Physical size of one map tile. All scale-dependent output derives from this.
constexpr float kMetersPerTile = 50.0f;

inline float tileToKm(int tiles) {
  return static_cast<float>(tiles) * kMetersPerTile / 1000.0f;
}

inline float mapAreaKm2(int widthTiles, int heightTiles) {
  return tileToKm(widthTiles) * tileToKm(heightTiles);
}
