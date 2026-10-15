#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include "src/gameplay/CityHistory.hpp"
#include "src/visualization/DrawList.hpp"

namespace visualizer {
inline Rect trendChartRect(int chart,int width,int height) {
  return {(width-720)/2+24+(chart%2)*340,(height-540)/2+148+(chart/2)*110,309,61};
}
inline size_t trendSampleIndex(const CityHistory& history,uint32_t tick) {
  if (history.samples.empty()) return 0;
  if (!tick) return history.samples.size()-1;
  const auto found=std::lower_bound(history.samples.begin(),history.samples.end(),tick,
    [](const auto& sample,uint32_t t){return sample.tick<t;});
  return found==history.samples.end() ? history.samples.size()-1 : static_cast<size_t>(found-history.samples.begin());
}
inline uint32_t trendHitTick(int x,int y,int width,int height,const CityHistory& history) {
  if (history.samples.empty()) return 0;
  for (int chart=0;chart<6;++chart) {
    const auto r=trendChartRect(chart,width,height);
    if (x<r.x || x>=r.x+r.w || y<r.y || y>=r.y+r.h) continue;
    const auto i=static_cast<size_t>(std::llround(static_cast<double>(x-r.x)*(history.samples.size()-1)/(r.w-1)));
    return history.samples[std::min(i,history.samples.size()-1)].tick;
  }
  return 0;
}
inline std::string trendTaxDetail(const CityHistory& history,size_t index) {
  if (!index) return "BASELINE: NO EARLIER POLICY RETAINED.";
  const auto& a=history.samples[index-1].policy.taxPercent;
  const auto& b=history.samples[index].policy.taxPercent;
  const char* names[]={"R","C","I","O","INC"};
  std::string text="TAX CHANGES:";
  for (size_t i=0;i<a.size();++i) if (a[i]!=b[i]) text+=" "+std::string(names[i])+" "+std::to_string(a[i])+" TO "+std::to_string(b[i]);
  return text=="TAX CHANGES:" ? "NO TAX CHANGE ON SELECTED TICK." : text;
}
inline std::string trendFundingDetail(const CityHistory& history,size_t index) {
  if (!index) return "GOLD MARKS THE FIRST TICK USING A CHANGED POLICY.";
  const auto& a=history.samples[index-1].policy.servicePercent;
  const auto& b=history.samples[index].policy.servicePercent;
  size_t first=a.size(),count=0;
  for (size_t i=0;i<a.size();++i) if (a[i]!=b[i]) {if (!count) first=i;++count;}
  if (!count) return "NO FUNDING CHANGE ON SELECTED TICK.";
  return std::string("FUNDING ")+ServiceSystem::serviceTypeToString(static_cast<ServiceType>(first))+" "+
    std::to_string(a[first])+" TO "+std::to_string(b[first])+(count>1 ? " +"+std::to_string(count-1)+" MORE DEPARTMENTS" : "");
}
} // namespace visualizer
