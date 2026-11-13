#pragma once
#include <nlohmann/json.hpp>
#include "src/world/Utility.hpp"
inline void to_json(nlohmann::json& j,const UtilityCell& c) {
  j={{"waterPipe",c.waterPipe},{"sewerPipe",c.sewerPipe},{"sewageLift",c.sewageLift},{"waterBooster",c.waterBooster},{"sewageBacklog",c.sewageBacklog}};
}
inline void from_json(const nlohmann::json& j,UtilityCell& c) {
  c.waterPipe=j.at("waterPipe").get<bool>();c.sewerPipe=j.at("sewerPipe").get<bool>();
  c.waterBooster=j.value("waterBooster",false);
  c.sewageLift=j.value("sewageLift",false);
  c.sewageBacklog=j.at("sewageBacklog").get<double>();
}
