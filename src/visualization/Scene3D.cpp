#include "src/visualization/Scene3DPrimitives.hpp"
#include "src/visualization/BuildingModels3D.hpp"
#include "src/visualization/Scene3D.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/systems/ParkSystem.hpp"
#include <array>

namespace visualizer {
namespace {
using namespace scene3d;
// Small procedural landmarks retain their actual simulation footprint.
void serviceSite(DrawList* l, const Camera3D& c, const ServiceFacility& f) {
  const float x=float(f.position.x), z=float(f.position.y);
  const float span=float(facilityFootprintSpan(f));
  const RGB color=serviceFacilityColor(f.type), concrete{190,193,178};
  slab(l,c,x+.04f,z+.04f,span-.08f,span-.08f,.021f,{136,147,132});
  if(f.type==ServiceType::Power && f.powerSource==PowerSourceType::Solar) {
    for(float dx=.12f;dx+.36f<=span-.08f;dx+=.45f)
      for(float dz=.12f;dz+.42f<=span-.08f;dz+=.55f) {
        box(l,c,x+dx+.14f,z+dz+.1f,.035f,.2f,.025f,.2f,concrete);
        quad(l,c,{{{x+dx,.2f,z+dz},{x+dx+.36f,.32f,z+dz},
                   {x+dx+.36f,.32f,z+dz+.42f},{x+dx,.2f,z+dz+.42f}}},{53,89,139});
      }
    return;
  }
  if(f.type==ServiceType::Power && f.powerSource==PowerSourceType::Wind) {
    box(l,c,x+.45f,z+.45f,.1f,.1f,.025f,1.2f,concrete);
    box(l,c,x+.4f,z+.4f,.2f,.2f,1.18f,.16f,{225,230,214});
    const float angle=float(l->frameTimeMs%8000u)*.00078539816f;
    for(int blade=0;blade<3;++blade) {
      const float a=angle+blade*2.0943951f;
      const float dx=std::cos(a)*.47f, dy=std::sin(a)*.47f;
      const float wx=-std::sin(a)*.035f, wy=std::cos(a)*.035f;
      quad(l,c,{{{x+.5f+wx,1.25f+wy,z+.385f},{x+.5f+dx+wx,1.25f+dy+wy,z+.385f},
                 {x+.5f+dx-wx,1.25f+dy-wy,z+.385f},{x+.5f-wx,1.25f-wy,z+.385f}}},{232,234,220});
    }
    return;
  }
  if(f.type==ServiceType::Water && !f.surfaceWater) {
    for(float dx:{.23f,.69f}) for(float dz:{.23f,.69f})
      box(l,c,x+dx,z+dz,.07f,.07f,.025f,.65f,concrete);
    box(l,c,x+.16f,z+.16f,.68f,.68f,.65f,.32f,color);
    box(l,c,x+.27f,z+.27f,.46f,.46f,.97f,.08f,{209,225,221});
    return;
  }
  if(f.type==ServiceType::Cemetery) {
    slab(l,c,x+.45f,z+.08f,.1f,.84f,.03f,{206,197,166});
    for(float dx:{.18f,.65f}) for(float dz:{.2f,.48f,.76f})
      box(l,c,x+dx,z+dz,.16f,.07f,.03f,.19f,{190,196,178});
    return;
  }
  if(f.type==ServiceType::Garbage) {
    const float fill=f.landfillCapacity>0 ? std::clamp(1.f-float(f.landfillRemaining)/float(f.landfillCapacity),0.f,1.f) : 0.f;
    box(l,c,x+.12f,z+.12f,.76f,.76f,.025f,.08f+fill*.3f,{136,112,77});
    box(l,c,x+.32f,z+.32f,.36f,.36f,.105f+fill*.3f,.06f,{155,140,99});
    return;
  }
  if(f.type==ServiceType::Sanitation || (f.type==ServiceType::Water && f.surfaceWater)) {
    box(l,c,x+.1f,z+.1f,.3f,.76f,.025f,.24f,color);
    for(float dz:{.16f,.57f}) {
      box(l,c,x+.5f,z+dz,.36f,.28f,.025f,.15f,concrete);
      slab(l,c,x+.54f,z+dz+.04f,.28f,.2f,.176f,
           f.type==ServiceType::Water ? RGB{90,164,194} : RGB{123,147,115});
    }
    return;
  }
  if(f.type==ServiceType::Power && f.powerSource==PowerSourceType::Substation) {
    for(float dz:{.18f,.58f}) {
      box(l,c,x+.18f,z+dz,.64f,.19f,.025f,.28f,{129,145,146});
      for(float dx:{.26f,.62f}) box(l,c,x+dx,z+dz+.05f,.08f,.08f,.305f,.18f,{207,186,115});
    }
    return;
  }
  if(f.type==ServiceType::Power && f.powerSource==PowerSourceType::Hydro) {
    box(l,c,x+.1f,z+.18f,.8f,.25f,.025f,.6f,concrete);
    for(float dx:{.2f,.45f,.7f}) box(l,c,x+dx,z+.43f,.12f,.36f,.025f,.25f,{87,161,187});
    return;
  }
  if(f.type==ServiceType::Power && f.powerSource==PowerSourceType::Nuclear) {
    box(l,c,x+.12f,z+.12f,.76f,.35f,.025f,.3f,color);
    for(float dx:{.18f,.58f}) {
      box(l,c,x+dx,z+.56f,.26f,.3f,.025f,.45f,concrete);
      box(l,c,x+dx+.035f,z+.595f,.19f,.23f,.475f,.2f,{169,177,164});
      slab(l,c,x+dx+.065f,z+.625f,.13f,.17f,.676f,{96,108,106});
    }
    return;
  }
  const bool industrial=f.type==ServiceType::Power || f.type==ServiceType::Incinerator ||
                        f.type==ServiceType::Crematorium || f.type==ServiceType::Recycling;
  box(l,c,x+.12f,z+.15f,.76f,.68f,.025f,industrial ? .38f : .5f,color);
  if(industrial) {
    const int stacks=(f.type==ServiceType::Power && f.powerSource==PowerSourceType::Coal) || f.type==ServiceType::Incinerator ? 2 : 1;
    for(int i=0;i<stacks;++i) {
      box(l,c,x+.22f+i*.33f,z+.25f,.12f,.14f,.405f,.45f,concrete);
      box(l,c,x+.22f+i*.33f,z+.25f,.12f,.14f,.855f,.07f,{127,79,63});
    }
    if(f.type==ServiceType::Power && f.powerSource==PowerSourceType::NaturalGas) {
      for(float dz:{.51f,.68f}) box(l,c,x+.37f,z+dz,.4f,.12f,.405f,.12f,{165,183,176});
    }
    if(f.type==ServiceType::Recycling) {
      box(l,c,x+.2f,z+.86f,.23f,.1f,.025f,.16f,{76,146,103});
      box(l,c,x+.57f,z+.86f,.23f,.1f,.025f,.16f,{68,126,170});
    }
  } else if(f.type==ServiceType::Health) {
    slab(l,c,x+.43f,z+.28f,.14f,.42f,.527f,{212,67,60});
    slab(l,c,x+.29f,z+.42f,.42f,.14f,.528f,{212,67,60});
  } else if(f.type==ServiceType::Fire) {
    for(float dx:{.2f,.43f,.66f})
      box(l,c,x+dx,z+.84f,.14f,.015f,.025f,.27f,{76,78,76});
  } else if(f.type==ServiceType::Education) {
    box(l,c,x+.4f,z+.35f,.2f,.2f,.525f,.3f,{220,201,129});
    box(l,c,x+.45f,z+.4f,.1f,.1f,.825f,.08f,{107,130,152});
  } else if(f.type==ServiceType::Police) {
    box(l,c,x+.22f,z+.24f,.035f,.035f,.525f,.45f,concrete);
    quad(l,c,{{{x+.255f,.97f,z+.24f},{x+.45f,.97f,z+.24f},
               {x+.45f,.83f,z+.24f},{x+.255f,.83f,z+.24f}}},{104,162,213});
  }
}

void terminalSite(DrawList* l, const Camera3D& c, const regional::Connection& terminal) {
  if(!terminal.constructed || terminal.kind==regional::Kind::RoadGateway) return;
  const float x=float(terminal.origin.x), z=float(terminal.origin.y);
  const float width=float(terminal.width), depth=float(terminal.height);
  const RGB roof=terminal.enabled ? RGB{162,194,202} : RGB{130,132,129};
  if(terminal.kind==regional::Kind::Airport) {
    slab(l,c,x+.04f,z+.04f,width-.08f,depth-.08f,.024f,{119,135,136});
    slab(l,c,x+.08f,z+depth*.48f,width-.16f,depth*.42f,.028f,{60,69,76});
    for(float dx=.18f;dx<width-.2f;dx+=.4f)
      slab(l,c,x+dx,z+depth*.69f,.18f,.035f,.032f,{235,225,192});
    for(float dz:{.55f,.64f,.73f}) {
      slab(l,c,x+.14f,z+depth*dz,.1f,.07f,.033f,{223,224,210});
      slab(l,c,x+width-.24f,z+depth*dz,.1f,.07f,.033f,{223,224,210});
    }
    box(l,c,x+.2f,z+.13f,width*.62f,depth*.22f,.025f,.3f,roof);
    box(l,c,x+width-.52f,z+.16f,.22f,.25f,.025f,.75f,{193,192,170});
    box(l,c,x+width-.6f,z+.12f,.38f,.33f,.775f,.18f,roof);
  } else {
    box(l,c,x+.04f,z+.04f,width-.08f,depth-.08f,-.02f,.1f,{137,151,146});
    if(terminal.kind==regional::Kind::CargoPort) {
      for(float dx=.15f;dx<width-.3f;dx+=.5f)
        for(float dz=.15f;dz<depth-.3f;dz+=.5f)
          box(l,c,x+dx,z+dz,.38f,.32f,.08f,.23f,terminal.enabled ? RGB{185,131,76} : roof);
      box(l,c,x+width-.35f,z+.16f,.1f,.1f,.08f,.95f,{215,184,103});
      box(l,c,x+width-.8f,z+.16f,.7f,.1f,1.03f,.08f,{215,184,103});
    } else {
      box(l,c,x+.15f,z+.15f,width-.3f,depth*.45f,.08f,.28f,roof);
      box(l,c,x+.1f,z+.1f,width-.2f,depth*.5f,.36f,.07f,{203,210,191});
      for(float dx=.2f;dx<width-.15f;dx+=.4f)
        box(l,c,x+dx,z+depth-.16f,.08f,.08f,.08f,.15f,{216,214,182});
    }
  }
}
}
void drawGroundHighlight(DrawList* list,const Camera3D& c,Coord t,RGB color,uint8_t alpha) {
  quad(list,c,{{{float(t.x),.04f,float(t.y)},{t.x+1.f,.04f,float(t.y)},
               {t.x+1.f,.04f,t.y+1.f},{float(t.x),.04f,t.y+1.f}}},color,alpha,true);
}
void drawCity3D(DrawList* l,const Camera3D& c,const CityMap& map,const RoadNetwork& roads,
                const EntityStore& store,const std::vector<ServiceFacility>& facilities,OverlayMode mode,
                const std::unordered_map<Coord,float,Vec2Hash>& heat,float penalty,
                const DistrictSystem& districts,DistrictId selectedDistrict,const TransitMarks* transit,
                const std::vector<regional::Connection>* terminals,ScenePicking3D* picking) {
  PickCollector collector(l,picking);
  PickParcel background(l,{-1,-1});
  const auto size=map.getDimensions();
  // A solid plinth gives the flat simulation map a visible edge from every angle.
  box(l,c,0,0,float(size.x),float(size.y),-.35f,.27f,{91,108,83});
  std::unordered_map<Coord,const ServiceFacility*,Vec2Hash> sites;
  for(const auto& f:facilities) {
    const int span=facilityFootprintSpan(f);
    for(int dz=0;dz<span;++dz) for(int dx=0;dx<span;++dx)
      sites[{f.position.x+dx,f.position.y+dz}]=&f;
  }
  for(int z=0;z<size.y;++z) for(int x=0;x<size.x;++x) {
    const Coord t{x,z};const auto& tile=map.getTile(t);
    PickParcel parcel(l,t);
    const auto center=c.project({x+.5f,0,z+.5f});
    // Keep offscreen towers: their roof can extend several tiles above the ground.
    if(!c.perspective && (center.x < -c.scale*8 || center.x > l->viewportWidth+c.scale*8 ||
       center.y < -c.scale*2 || center.y > l->viewportHeight+c.scale*12)) continue;
    const RGB ground=tileColor(map,roads,store,t,mode==OverlayMode::Transit ? OverlayMode::Zone : mode,facilities,heat,penalty);
    slab(l,c,float(x),float(z),1,1,tile.type==2 ? -.025f : 0.f,ground);
    if(tile.type==2) {
      slab(l,c,x+.15f,z+.35f,.4f,.025f,-.02f,{133,185,213});
      continue;
    }
    if(tile.hasRoad) {
      bool arterial=false;
      for(Coord d:{Coord{1,0},Coord{-1,0},Coord{0,1},Coord{0,-1}}) {
        Coord n{t.x+d.x,t.y+d.y};
        if(roads.hasRoad(t,n)) arterial |= roads.getRoadClass(t,n)==RoadClass::Arterial;
      }
      const float width=arterial ? .76f : .55f, margin=(1-width)*.5f;
      const RGB asphalt=(mode==OverlayMode::Zone || mode==OverlayMode::Transit) ? RGB{57,63,70} : ground;
      const RGB stripe{223,202,133};
      slab(l,c,x+margin,z+margin,width,width,.012f,asphalt);
      for(Coord d:{Coord{1,0},Coord{-1,0},Coord{0,1},Coord{0,-1}}) {
        const Coord n{t.x+d.x,t.y+d.y};
        if(!map.isValid(n)||!map.getTile(n).hasRoad) continue;
        if(d.x) {
          slab(l,c,x+(d.x>0 ? .5f : 0.f),z+margin,.5f,width,.012f,asphalt);
          slab(l,c,x+(d.x>0 ? .65f : .1f),z+.49f,.23f,.025f,.016f,stripe);
        } else {
          slab(l,c,x+margin,z+(d.y>0 ? .5f : 0.f),width,.5f,.012f,asphalt);
          slab(l,c,x+.49f,z+(d.y>0 ? .65f : .1f),.025f,.23f,.016f,stripe);
        }
      }
    }
    const auto* b=store.getBuilding(tile.buildingId);
    if(b) {
      RGB facade=b->monumentKind ? RGB{126,180,190} : b->uniqueKind ? RGB{207,173,102} :
        b->type==BuildingType::Residential ? RGB{220,189,151} :
        b->type==BuildingType::Commercial ? RGB{109,164,190} :
        b->type==BuildingType::Office ? RGB{143,159,194} : RGB{178,148,108};
      const bool diagnostic=mode!=OverlayMode::Zone && mode!=OverlayMode::Transit;
      if(diagnostic) facade=ground;
      drawBuilding3D(l,c,*b,facade,diagnostic);
    }
    if(ParkSystem::isPark(map,t) || (!sites.count(t)&&!tile.hasRoad&&!tile.buildingId&&!tile.regionalConnectionId&&tile.zone==0&&tile.type==0&&((x*31+z*17)%19==0))) {
      if(ParkSystem::isPark(map,t)) slab(l,c,x+.44f,float(z),.12f,1,.02f,{205,192,150});
      box(l,c,x+.24f,z+.3f,.07f,.07f,.01f,.25f,{115,89,61});
      box(l,c,x+.12f,z+.18f,.32f,.32f,.24f,.32f,{78,138,76});
      box(l,c,x+.18f,z+.24f,.2f,.2f,.56f,.13f,{100,161,88});
    }
    if(transit) {
      const auto marks=transit->find(t);
      if(marks!=transit->end()) for(const auto& mark:marks->second) {
        const RGB color=mark.mode==TransitMode::Rail ? RGB{250,192,60} : RGB{35,210,240};
        if(mark.stop) box(l,c,x+.4f,z+.4f,.2f,.2f,.03f,.12f,color);
        else if(mark.neighbor.x!=x) slab(l,c,float(x),z+.47f,1,.06f,.027f,color);
        else slab(l,c,x+.47f,float(z),.06f,1,.027f,color);
      }
    }
    for(const auto& district:districts.getDistricts()) {
      if(!district.contains(t)) continue;
      const RGB tint=district.id==selectedDistrict ? RGB{255,245,150} : districtArchetypeColor(district.archetype);
      if(x==district.minCorner.x) slab(l,c,float(x),float(z),.035f,1,.035f,tint);
      if(x==district.maxCorner.x) slab(l,c,x+.965f,float(z),.035f,1,.035f,tint);
      if(z==district.minCorner.y) slab(l,c,float(x),float(z),1,.035f,.035f,tint);
      if(z==district.maxCorner.y) slab(l,c,float(x),z+.965f,1,.035f,.035f,tint);
    }
  }
  // Draw entire sites after the ground: footprint corners may be offscreen and
  // water parcels must not hide a constructed waterfront terminal.
  for(const auto& f:facilities) {PickParcel site(l,f.position);serviceSite(l,c,f);}
  if(terminals) for(const auto& terminal:*terminals) {PickParcel site(l,terminal.origin);terminalSite(l,c,terminal);}
}
} // namespace visualizer
