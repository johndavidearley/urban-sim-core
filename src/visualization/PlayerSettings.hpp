#pragma once
#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/WindowHost.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace visualizer {
struct PlayerSettings {
  int resolution=0;
  bool borderless=false,readable=true;
  static constexpr std::array<Coord,3> sizes{{{1280,720},{1600,900},{1920,1080}}};
  static std::filesystem::path path(bool playtest) {
    if(playtest) return "urban_sim_settings.json";
#ifdef _WIN32
    if(const char* root=std::getenv("APPDATA")) return std::filesystem::path(root)/"UrbanSimCore/settings.json";
#elif defined(__APPLE__)
    if(const char* root=std::getenv("HOME")) return std::filesystem::path(root)/"Library/Application Support/UrbanSimCore/settings.json";
#else
    if(const char* root=std::getenv("XDG_CONFIG_HOME")) return std::filesystem::path(root)/"urban-sim/settings.json";
    if(const char* root=std::getenv("HOME")) return std::filesystem::path(root)/".config/urban-sim/settings.json";
#endif
    return "urban_sim_settings.json";
  }
  static PlayerSettings load(const std::filesystem::path& file) {
    PlayerSettings result;
    try {
      std::ifstream input(file);if(!input) return result;nlohmann::json j;input>>j;
      const int size=j.at("resolution").get<int>();if(size<0||size>=int(sizes.size())) return result;
      result.resolution=size;result.borderless=j.at("borderless").get<bool>();result.readable=j.at("readable").get<bool>();
    } catch(const std::exception&) {return {};}
    return result;
  }
  bool save(const std::filesystem::path& file) const {
    try {
      if(!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
      const auto temporary=file.string()+".tmp";
      {std::ofstream output(temporary);output<<nlohmann::json{{"resolution",resolution},{"borderless",borderless},{"readable",readable}}.dump(2);output.flush();if(!output) return false;}
      // Settings are recoverable preferences, separate from city saves.
      std::error_code error;std::filesystem::rename(temporary,file,error);
      if(error) {std::filesystem::remove(file,error);error.clear();std::filesystem::rename(temporary,file,error);}
      return !error;
    } catch(const std::exception&) {return false;}
  }
};
inline Rect settingsPanel(int width,int height) {return {(width-660)/2,(height-540)/2,660,540};}
inline Rect settingsButton(int action,int width,int height) {
  const auto p=settingsPanel(width,height);
  if(action==0) return {p.x+p.w-92,p.y+12,80,28};
  if(action==5) return {p.x+18,p.y+p.h-48,150,30};
  if(action==6) return {p.x+p.w-168,p.y+p.h-48,150,30};
  return {p.x+280,p.y+64+(action-1)*48,350,34};
}
inline int settingsHit(int x,int y,int width,int height,bool help) {
  for(int i=0;i<7;++i) {
    if(help && i>=1 && i<=4) continue;
    const auto r=settingsButton(i,width,height);
    if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h) return i;
  }
  return -1;
}
inline void drawSettings(DrawList* draw,int width,int height,const PlayerSettings& pending,bool help,const std::string& status) {
  drawFilledRect(draw,0,0,width,height,{5,10,16},215);
  const auto p=settingsPanel(width,height);drawFilledRect(draw,p.x,p.y,p.w,p.h,{23,33,44},255);
  drawRectOutline(draw,p.x,p.y,p.w,p.h,{130,195,225});
  drawText(draw,p.x+18,p.y+16,help?"CONTROLS AND RECOVERY":"PLAYER SETTINGS",{225,240,250},2);
  const auto button=[&](int action,const std::string& text) {
    const auto r=settingsButton(action,width,height);drawFilledRect(draw,r.x,r.y,r.w,r.h,{46,70,87});
    drawText(draw,r.x+8,r.y+8,text,{225,240,250},1);
  };
  button(0,"CLOSE");button(5,help?"SETTINGS":"CONTROLS GUIDE");button(6,help?"BACK TO CITY":"APPLY AND SAVE");
  if(!help) {
    const auto size=PlayerSettings::sizes[size_t(pending.resolution)];
    const std::array<std::string,4> labels{{"WINDOW RESOLUTION","DISPLAY MODE","UI TEXT","RESET PREFERENCES"}};
    for(int i=0;i<4;++i) drawText(draw,p.x+18,p.y+74+i*48,labels[size_t(i)],{190,210,225},1);
    button(1,std::to_string(size.x)+" X "+std::to_string(size.y)+" - CLICK TO CYCLE");
    button(2,pending.borderless?"BORDERLESS DESKTOP":"WINDOWED");
    button(3,pending.readable?"READABLE TEXT":"COMPACT TEXT");button(4,"RESTORE DEFAULTS");
    drawText(draw,p.x+18,p.y+280,"Borderless uses your monitor's desktop resolution.",{180,200,215},1);
    drawText(draw,p.x+18,p.y+304,"Apply saves preferences; closing discards pending edits.",{180,200,215},1);
    drawText(draw,p.x+18,p.y+328,"Actual window: "+std::to_string(width)+" x "+std::to_string(height)+" (desktop may limit size).",{180,200,215},1);
    drawText(draw,p.x+18,p.y+370,status,{245,210,130},1);
  } else {
    const std::array<const char*,16> lines{{
      "R ROADS: drag a street; A type, L layout, O snap.",
      "Z ZONING: repeat to choose homes, shops or industry.",
      "S SERVICES: repeat to choose power, water, sanitation.",
      "X PIPES: combined trenches; repeat for water/sewer.",
      "Pipe mode: L sewer lift, A booster, V lay/remove.",
      "W cycles wind, currents and utility diagnostics.",
      "No water? Join blue pipes to a source; check pressure.",
      "Sewage backup? Join orange pipes to a lower outlet.",
      "Lifts/boosters need power and connected pipes.",
      "No power? Check road reach, capacity and funding.",
      "Garbage? Add collection, check reach and free volume.",
      "F6 BUDGET: taxes, funding and operating costs.",
      "SPACE pause/play; N one tick; HUD buttons set speed.",
      "F5 quicksave; F9 quickload; F11 named saves/backups.",
      "Wheel zoom; arrows pan; F focus; T camera mode.",
      "F1 city guide; F4 detailed HUD; F10 settings/help."
    }};
    for(size_t i=0;i<lines.size();++i) drawText(draw,p.x+18,p.y+66+int(i)*24,lines[i],{205,220,235},1);
  }
}
} // namespace visualizer
