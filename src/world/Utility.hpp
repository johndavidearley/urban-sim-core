#pragma once
// Underground infrastructure is independent of roads and surface demolition.
struct UtilityCell {
  bool waterPipe=false;
  bool sewerPipe=false;
  bool sewageLift=false;
  bool waterBooster=false;
  double sewageBacklog=0; // Persistent m3 awaiting a reachable outlet.
  // Derived, read-only diagnostics between simulation ticks.
  bool liftPowered=false,boosterPowered=false;
  float waterDemand=0,waterDelivered=0,sewageHandled=0,liftFlow=0,boosterFlow=0,waterPressure=0;
};
