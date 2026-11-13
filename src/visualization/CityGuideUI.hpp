#pragma once
#include "src/gameplay/CityGuide.hpp"
#include "src/visualization/VisualizerRender.hpp"
namespace visualizer {
inline Rect guidePanel(int height) {return {14,height-216,520,148};}
inline Rect guideButton(int action,int height) {
  const auto p=guidePanel(height);
  if(action==2) return {p.x+p.w-64,p.y+10,54,24};
  return {p.x+12+action*210,p.y+108,198,28};
}
inline bool overGuide(int x,int y,int height) {const auto p=guidePanel(height);return x>=p.x&&x<p.x+p.w&&y>=p.y&&y<p.y+p.h;}
inline int guideHit(int x,int y,int height) {
  for(int i=0;i<3;++i) {const auto r=guideButton(i,height);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h) return i;}
  return -1;
}
}
