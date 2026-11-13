#pragma once
#include "src/persistence/GameSaveStore.hpp"
#include "src/visualization/VisualizerRender.hpp"
#include "src/visualization/WindowHost.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace visualizer {
struct SaveBrowser {
  static constexpr int pageSize=7;
  std::vector<GameSaveStore::Entry> rows;
  int selected=-1,page=0;
  bool editing=false;
  std::string name="City",status,confirmation;
  static std::string folded(std::string value) {
    for(auto& c:value) if(c>='A' && c<='Z') c=char(c-'A'+'a');
    return value;
  }
  void refresh(const GameSaveStore& store) {
    try {rows=store.entries();status=rows.empty()?"No saved cities yet.":"Choose a city or backup; enter a name to save.";}
    catch(const std::exception&) {rows.clear();status="Cannot read the save folder.";}
    page=0;selected=rows.empty()?-1:0;confirmation.clear();editing=false;
  }
  static Rect panel(int width,int height) {return {(width-760)/2,(height-560)/2,760,560};}
  static Rect control(int action,int width,int height) {
    const auto p=panel(width,height);
    if(action==0) return {p.x+658,p.y+14,86,30};
    if(action==1) return {p.x+18,p.y+500,170,34};
    if(action==2) return {p.x+550,p.y+500,192,34};
    if(action==3) return {p.x+18,p.y+444,104,30};
    if(action==4) return {p.x+132,p.y+444,104,30};
    if(action==5) return {p.x+650,p.y+394,92,34};
    if(action==6) return {p.x+18,p.y+394,620,34};
    if(action==7) return {p.x+638,p.y+444,104,30};
    return {p.x+18,p.y+96+(action-100)*38,724,34};
  }
  static bool contains(const Rect& r,int x,int y) {return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;}
  int input(const InputEvent& event,int width,int height,bool canSave) {
    if(event.type==EventType::TextInput && editing && canSave) {
      if(name.size()<48 && event.codepoint<128) {
        const char c=char(event.codepoint);
        if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c==' ' || c=='_' || c=='-') {
          name+=c;confirmation.clear();status="Enter a name, then choose Save city.";return -1;
        }
      }
      status="Use up to 48 English letters, numbers, spaces, - or _.";
    }
    int action=-1;
    if(event.type==EventType::KeyDown) {
      if(event.key.sym==Key::Escape || event.key.sym==Key::F11) return 0;
      if(event.key.sym==Key::Tab && canSave) editing=!editing;
      if(event.key.sym==Key::Backspace && editing && !name.empty()) {name.pop_back();confirmation.clear();}
      if(event.key.sym==Key::Enter) return editing && canSave?2:1;
      if(event.key.sym==Key::PageUp) action=3;
      if(event.key.sym==Key::PageDown) action=4;
      if(!editing && (event.key.sym==Key::Up || event.key.sym==Key::Down) && !rows.empty()) {
        selected=std::clamp(selected+(event.key.sym==Key::Down?1:-1),0,int(rows.size())-1);
        page=selected/pageSize;confirmation.clear();
        if(canSave && folded(rows[size_t(selected)].name)!="autosave") name=rows[size_t(selected)].name;
      }
    }
    if(event.type==EventType::MouseDown && event.button.button==MouseButton::Left) {
      for(int i=0;i<8;++i) if(contains(control(i,width,height),event.button.x,event.button.y)) action=i;
      for(int i=0;i<pageSize && page*pageSize+i<int(rows.size());++i)
        if(contains(control(100+i,width,height),event.button.x,event.button.y)) action=100+i;
    }
    if(action>=100) {
      selected=page*pageSize+action-100;editing=false;confirmation.clear();
      if(canSave && folded(rows[size_t(selected)].name)!="autosave") name=rows[size_t(selected)].name;
      return -1;
    }
    if(action==3 || action==4) {
      page=std::clamp(page+(action==3?-1:1),0,std::max(0,(int(rows.size())-1)/pageSize));
      selected=rows.empty()?-1:page*pageSize;confirmation.clear();editing=false;return -1;
    }
    if(action==5 && canSave) {name.clear();editing=true;confirmation.clear();return -1;}
    if(action==6 && canSave) {editing=true;confirmation.clear();return -1;}
    if(action==2 && !canSave) return -1;
    return action;
  }
};
inline void drawSaveBrowser(DrawList* draw,int width,int height,const SaveBrowser& browser,bool canSave) {
  drawFilledRect(draw,0,0,width,height,{5,10,16},220);
  const auto p=SaveBrowser::panel(width,height);
  drawFilledRect(draw,p.x,p.y,p.w,p.h,{23,33,44},255);drawRectOutline(draw,p.x,p.y,p.w,p.h,{130,195,225});
  drawText(draw,p.x+18,p.y+20,"SAVED CITIES",{225,240,250},2);
  for(const auto& heading:std::array<std::pair<int,const char*>,4>{{{26,"CITY"},{348,"GENERATION"},{478,"TICK"},{612,"MAP"}}})
    drawText(draw,p.x+heading.first,p.y+60,heading.second,{175,205,225},1);
  const auto button=[&](int action,const std::string& label,bool enabled=true) {
    const auto r=SaveBrowser::control(action,width,height);drawFilledRect(draw,r.x,r.y,r.w,r.h,enabled?RGB{46,70,87}:RGB{35,42,50});
    drawText(draw,r.x+8,r.y+9,label,enabled?RGB{225,240,250}:RGB{120,130,140},1);
  };
  button(0,"CLOSE");button(1,browser.confirmation.rfind("load:",0)==0?"CONFIRM LOAD":"LOAD SELECTED");button(3,"PREVIOUS");button(4,"NEXT PAGE");button(7,"REFRESH");
  for(int i=0;i<SaveBrowser::pageSize && browser.page*SaveBrowser::pageSize+i<int(browser.rows.size());++i) {
    const int index=browser.page*SaveBrowser::pageSize+i;const auto& row=browser.rows[size_t(index)];
    const auto r=SaveBrowser::control(100+i,width,height);
    drawFilledRect(draw,r.x,r.y,r.w,r.h,index==browser.selected?RGB{55,82,99}:RGB{32,44,55});
    const RGB color=row.valid?RGB{215,235,245}:RGB{230,160,135};
    drawText(draw,r.x+8,r.y+10,row.name.substr(0,29),color,1);
    drawText(draw,r.x+330,r.y+10,row.backup?"BACKUP "+std::to_string(row.backup):"LATEST",color,1);
    drawText(draw,r.x+460,r.y+10,row.valid?std::to_string(row.tick):"UNAVAILABLE",color,1);
    if(row.valid) drawText(draw,r.x+594,r.y+10,std::to_string(row.width)+" X "+std::to_string(row.height),color,1);
  }
  drawText(draw,p.x+18,p.y+370,canSave?"SAVE NAME - click to type (48 characters maximum)":"Select a city to load. Unavailable entries need a valid backup.",{185,210,225},1);
  if(canSave) {
    const auto r=SaveBrowser::control(6,width,height);drawFilledRect(draw,r.x,r.y,r.w,r.h,{15,25,35});
    drawRectOutline(draw,r.x,r.y,r.w,r.h,browser.editing?RGB{245,215,120}:RGB{90,120,140});
    drawText(draw,r.x+8,r.y+10,browser.name+(browser.editing?"_":""),{235,240,245},1);
    button(5,"CLEAR");button(2,browser.confirmation.rfind("save:",0)==0?"CONFIRM REPLACE":"SAVE CITY");
  }
  drawText(draw,p.x+260,p.y+454,"PAGE "+std::to_string(browser.page+1)+" / "+std::to_string(std::max(1,(int(browser.rows.size())+SaveBrowser::pageSize-1)/SaveBrowser::pageSize)),{180,205,225},1);
  drawText(draw,p.x+18,p.y+480,browser.status.substr(0,86),{245,210,130},1);
  drawText(draw,p.x+204,p.y+512,browser.editing?"ENTER SAVE   TAB SELECT   ESC CLOSE":"ESC CLOSE   UP/DOWN SELECT   ENTER LOAD",{170,195,215},1);
}
} // namespace visualizer
