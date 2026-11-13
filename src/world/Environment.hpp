#pragma once
#include <cstdint>

// Water volumes are m³. Pollutant mass is contaminant-equivalent m³; mass /
// volume is concentration. Currents are tile lengths per simulation tick.
struct EnvironmentCell {
  float terrainMeters=0.f, bedMeters=0.f;
  double water=0., contaminant=0.;
  float currentX=0.f,currentZ=0.f,windX=0.f,windZ=0.f;
};
struct EnvironmentState {
  bool initialized=false;
  uint32_t seed=0;
  uint64_t tick=0;
  float windX=0.f,windZ=0.f,windPhase=0.f;
  double inflowScale=1.,sewageBacklog=0.;
  double inflow=0.,outflow=0.,withdrawal=0.,discharge=0.;
  double contaminantIn=0.,contaminantOut=0.,contaminantDecay=0.;
  double sewageGenerated=0.,sewageHandled=0.;
};
