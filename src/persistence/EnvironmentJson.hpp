#pragma once
#include <stdexcept>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include "src/world/Environment.hpp"

inline void to_json(nlohmann::json& j,const EnvironmentCell& c) {
  j={{"terrainMeters",c.terrainMeters},{"bedMeters",c.bedMeters},{"water",c.water},{"contaminant",c.contaminant},
     {"currentX",c.currentX},{"currentZ",c.currentZ},{"windX",c.windX},{"windZ",c.windZ}};
}
inline void from_json(const nlohmann::json& j,EnvironmentCell& c) {
  c.terrainMeters=j.at("terrainMeters").get<float>();c.bedMeters=j.at("bedMeters").get<float>();
  c.water=j.at("water").get<double>();c.contaminant=j.at("contaminant").get<double>();
  c.currentX=j.at("currentX").get<float>();c.currentZ=j.at("currentZ").get<float>();
  c.windX=j.at("windX").get<float>();c.windZ=j.at("windZ").get<float>();
}
inline void to_json(nlohmann::json& j,const EnvironmentState& s) {
  j={{"version",1},{"initialized",s.initialized},{"seed",s.seed},{"tick",s.tick},
    {"windX",s.windX},{"windZ",s.windZ},{"windPhase",s.windPhase},{"inflowScale",s.inflowScale},
    {"sewageBacklog",s.sewageBacklog},{"inflow",s.inflow},{"outflow",s.outflow},
    {"withdrawal",s.withdrawal},{"discharge",s.discharge},{"contaminantIn",s.contaminantIn},
    {"contaminantOut",s.contaminantOut},{"contaminantDecay",s.contaminantDecay},
    {"sewageGenerated",s.sewageGenerated},{"sewageHandled",s.sewageHandled}};
}
inline void from_json(const nlohmann::json& j,EnvironmentState& s) {
  if(j.at("version").get<int>()!=1) throw std::invalid_argument("unsupported environment version");
  if(!j.at("seed").is_number_integer() || j.at("seed").get<int64_t>()<0 ||
     j.at("seed").get<uint64_t>()>std::numeric_limits<uint32_t>::max() || !j.at("tick").is_number_integer())
    throw std::invalid_argument("invalid environment seed or tick");
  s.initialized=j.at("initialized").get<bool>();s.seed=j.at("seed").get<uint32_t>();s.tick=j.at("tick").get<uint64_t>();
  s.windX=j.at("windX").get<float>();s.windZ=j.at("windZ").get<float>();s.windPhase=j.at("windPhase").get<float>();
  s.inflowScale=j.at("inflowScale").get<double>();s.sewageBacklog=j.at("sewageBacklog").get<double>();
  s.inflow=j.at("inflow").get<double>();s.outflow=j.at("outflow").get<double>();
  s.withdrawal=j.at("withdrawal").get<double>();s.discharge=j.at("discharge").get<double>();
  s.contaminantIn=j.at("contaminantIn").get<double>();s.contaminantOut=j.at("contaminantOut").get<double>();
  s.contaminantDecay=j.at("contaminantDecay").get<double>();s.sewageGenerated=j.at("sewageGenerated").get<double>();
  s.sewageHandled=j.at("sewageHandled").get<double>();
}
inline bool validEnvironment(const EnvironmentState& s) {
  if(!s.initialized) return true;
  if(s.tick>1000000000000ULL || !std::isfinite(s.inflowScale) || s.inflowScale<0 || s.inflowScale>2 ||
    !std::isfinite(s.windX)||!std::isfinite(s.windZ)||std::abs(s.windX)>1000||std::abs(s.windZ)>1000 ||
    !std::isfinite(s.windPhase)||s.windPhase<0||s.windPhase>6.284) return false;
  for(double v:{s.sewageBacklog,s.inflow,s.outflow,s.withdrawal,s.discharge,s.contaminantIn,s.contaminantOut,
                s.contaminantDecay,s.sewageGenerated,s.sewageHandled})
    if(!std::isfinite(v)||v<0||v>1e15) return false;
  return true;
}
inline bool validEnvironment(const EnvironmentCell& c,bool water) {
  if(!std::isfinite(c.terrainMeters)||!std::isfinite(c.bedMeters)||std::abs(c.terrainMeters)>100000||std::abs(c.bedMeters)>100000 ||
     !std::isfinite(c.water)||!std::isfinite(c.contaminant)||c.water<0||c.contaminant<0||c.water>1e12||c.contaminant>1e12) return false;
  for(float v:{c.currentX,c.currentZ,c.windX,c.windZ}) if(!std::isfinite(v)||std::abs(v)>1000) return false;
  return water || (c.water==0 && c.contaminant==0);
}
