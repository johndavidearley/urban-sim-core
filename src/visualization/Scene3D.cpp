#include "src/systems/EnvironmentSystem.hpp"
#include "src/visualization/RoadAlignmentRender.hpp"
#include "src/visualization/RoadCurves.hpp"
#include "src/visualization/Scene3DPrimitives.hpp"
#include "src/visualization/BuildingModels3D.hpp"
#include "src/visualization/Scene3D.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/systems/ParkSystem.hpp"
#include <array>
#include <unordered_set>

namespace visualizer {
namespace {
using namespace scene3d;
// Small procedural landmarks retain their actual simulation footprint.
void serviceSite(DrawList* l, const Camera3D& c, const ServiceFacility& f,const CityMap* map=nullptr) {
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
    const auto local=map && map->environment().initialized ? map->environment(f.position) : EnvironmentCell{};
    const float norm=std::sqrt(local.windX*local.windX+local.windZ*local.windZ);
    const float sideX=norm>.001f ? -local.windZ/norm : 1.f,sideZ=norm>.001f ? local.windX/norm : 0.f;
    const float angle=map && map->environment().initialized ?
      (EnvironmentSystem::windPowerFactor(norm)>0 ? map->environment().windPhase : 0.f) : float(l->frameTimeMs%8000u)*.00078539816f;
    for(int blade=0;blade<3;++blade) {
      const float a=angle+blade*2.0943951f;
      const float dx=std::cos(a)*.47f, dy=std::sin(a)*.47f;
      const float wx=-std::sin(a)*.035f, wy=std::cos(a)*.035f;
      quad(l,c,{{{x+.5f+wx*sideX,1.25f+wy,z+.5f+wx*sideZ},
                 {x+.5f+(dx+wx)*sideX,1.25f+dy+wy,z+.5f+(dx+wx)*sideZ},
                 {x+.5f+(dx-wx)*sideX,1.25f+dy-wy,z+.5f+(dx-wx)*sideZ},
                 {x+.5f-wx*sideX,1.25f-wy,z+.5f-wx*sideZ}}},{232,234,220});
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
  if (f.type == ServiceType::Health && f.clinic) {
    box(l,c,x+.22f,z+.25f,.56f,.48f,.025f,.24f,{187,222,214});
    slab(l,c,x+.44f,z+.36f,.12f,.26f,.267f,{61,145,131});
    slab(l,c,x+.37f,z+.43f,.26f,.12f,.268f,{61,145,131});
    box(l,c,x+.4f,z+.73f,.2f,.02f,.025f,.15f,{83,132,140});
    return;
  }
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
  std::unordered_set<Coord,Vec2Hash> cuts;
  for(const auto& alignment:roads.alignments()) if(alignment.elevation<0)
    cuts.insert(alignment.footprint.begin(),alignment.footprint.end());
  // Lower the base beneath open cuts so it cannot occlude underpass roads.
  box(l,c,0,0,float(size.x),float(size.y),cuts.empty() ? -.35f : -1.25f,cuts.empty() ? .27f : .2f,{91,108,83});
  if(!cuts.empty()) {
    const float w=float(size.x),d=float(size.y),bottom=-1.05f;
    quad(l,c,{{{0,0,0},{w,0,0},{w,bottom,0},{0,bottom,0}}},{91,108,83});
    quad(l,c,{{{w,0,0},{w,0,d},{w,bottom,d},{w,bottom,0}}},{101,118,91});
    quad(l,c,{{{w,0,d},{0,0,d},{0,bottom,d},{w,bottom,d}}},{109,124,96});
    quad(l,c,{{{0,0,d},{0,0,0},{0,bottom,0},{0,bottom,d}}},{87,103,80});
  }
  std::unordered_map<Coord,const ServiceFacility*,Vec2Hash> sites;
  for(const auto& f:facilities) {
    const int span=facilityFootprintSpan(f);
    for(int dz=0;dz<span;++dz) for(int dx=0;dx<span;++dx)
      sites[{f.position.x+dx,f.position.y+dz}]=&f;
  }
  // Distance into undeveloped rough terrain gives connected ridges rather
  // than separate tile-sized spikes. Developed parcels form level clearings.
  const int stride=size.x+2;
  std::vector<int> relief(static_cast<size_t>(stride)*(size.y+2),0);
  auto index=[&](int x,int z) {return static_cast<size_t>(z+1)*stride+x+1;};
  for(int z=0;z<size.y;++z) for(int x=0;x<size.x;++x) {
    const auto& tile=map.getTile({x,z});
    if(tile.type==1 && !tile.hasRoad && !tile.buildingId && !tile.zone &&
       !tile.regionalConnectionId && !sites.count({x,z})) relief[index(x,z)]=6;
  }
  for(int z=0;z<size.y;++z) for(int x=0;x<size.x;++x)
    relief[index(x,z)]=std::min(relief[index(x,z)],1+std::min(relief[index(x-1,z)],relief[index(x,z-1)]));
  for(int z=size.y-1;z>=0;--z) for(int x=size.x-1;x>=0;--x)
    relief[index(x,z)]=std::min(relief[index(x,z)],1+std::min(relief[index(x+1,z)],relief[index(x,z+1)]));
  const auto cornerHeight=[&](int x,int z) {
    const int shared=std::min({relief[index(x-1,z-1)],relief[index(x,z-1)],relief[index(x-1,z)],relief[index(x,z)]});
    if(map.environment().initialized && shared>0) {
      float height=100000.f;
      for(int dz:{-1,0}) for(int dx:{-1,0}) {
        const Coord p{x+dx,z+dz};
        if(map.isValid(p)) height=std::min(height,map.environment(p).terrainMeters/50.f);
      }
      return std::max(0.f,height);
    }
    return .55f*float(std::min({relief[index(x-1,z-1)],relief[index(x,z-1)],
                               relief[index(x-1,z)],relief[index(x,z)]}));
  };
  for(int z=0;z<size.y;++z) for(int x=0;x<size.x;++x) {
    const Coord t{x,z};const auto& tile=map.getTile(t);
    PickParcel parcel(l,t);
    const auto center=c.project({x+.5f,0,z+.5f});
    // Keep offscreen towers: their roof can extend several tiles above the ground.
    if(!c.perspective && (center.x < -c.scale*8 || center.x > l->viewportWidth+c.scale*8 ||
       center.y < -c.scale*2 || center.y > l->viewportHeight+c.scale*12)) continue;
    const RGB ground=tileColor(map,roads,store,t,mode==OverlayMode::Transit ? OverlayMode::Zone : mode,facilities,heat,penalty);
    if(relief[index(x,z)]>0) {
      const std::array<WorldPoint,4> corners{{
        {float(x),cornerHeight(x,z),float(z)}, {x+1.f,cornerHeight(x+1,z),float(z)},
        {x+1.f,cornerHeight(x+1,z+1),z+1.f}, {float(x),cornerHeight(x,z+1),z+1.f}}};
      // Smooth interpolation has matching heights and tangents on shared
      // edges. A gentle rounded crest replaces the old tile-center pyramid.
      const auto point=[&](float u,float v) -> WorldPoint {
        const float su=u*u*(3.f-2.f*u),sv=v*v*(3.f-2.f*v);
        const float near=corners[0].y+(corners[1].y-corners[0].y)*su;
        const float far=corners[3].y+(corners[2].y-corners[3].y)*su;
        const float crest=std::sin(u*3.14159265f)*std::sin(v*3.14159265f);
        return {x+u,near+(far-near)*sv+.18f*crest*crest,z+v};
      };
      const auto slope=[&](WorldPoint a,WorldPoint b,WorldPoint peak) {
        const float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
        const float vx=peak.x-a.x,vy=peak.y-a.y,vz=peak.z-a.z;
        const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        const float length=std::sqrt(nx*nx+ny*ny+nz*nz);
        const float light=.72f+.28f*std::clamp((-nx*.4f-ny*.82f-nz*.4f)/length,0.f,1.f);
        triangle(l,c,{{a,b,peak}},shade(ground,light));
      };
      constexpr int subdivisions=4;
      for(int v=0;v<subdivisions;++v) for(int u=0;u<subdivisions;++u) {
        const float u0=float(u)/subdivisions,u1=float(u+1)/subdivisions;
        const float v0=float(v)/subdivisions,v1=float(v+1)/subdivisions;
        const auto a=point(u0,v0),b=point(u1,v0),d=point(u0,v1),e=point(u1,v1);
        slope(a,b,e);slope(a,e,d);
      }
    } else if(cuts.count(t)) {
      constexpr float floor=-.95f;
      slab(l,c,float(x),float(z),1,1,floor,{116,115,99});
      for(Coord d:{Coord{1,0},Coord{-1,0},Coord{0,1},Coord{0,-1}}) {
        if(cuts.count({x+d.x,z+d.y})) continue;
        const float ax=d.x>0 ? x+1.f : float(x),az=d.y>0 ? z+1.f : float(z);
        const float bx=ax+(d.x==0 ? 1.f : 0.f),bz=az+(d.y==0 ? 1.f : 0.f);
        quad(l,c,{{{ax,0,az},{bx,0,bz},{bx,floor,bz},{ax,floor,az}}},{143,150,137});
      }
    } else slab(l,c,float(x),float(z),1,1,tile.type==2 ? -.025f : 0.f,ground);
    if(tile.type==2) {
      // Round each convex water corner into a quarter circle. The bank
      // occupies only the corner of its water parcel, so tile ownership and
      // the adjacent buildable land remain stable for tools and picking.
      for(int dz:{-1,1}) for(int dx:{-1,1}) {
        const Coord across{x+dx,z},along{x,z+dz};
        if(!map.isValid(across)||!map.isValid(along) ||
           map.getTile(across).type==2 || map.getTile(along).type==2) continue;
        const float cx=dx<0 ? float(x) : x+1.f,cz=dz<0 ? float(z) : z+1.f;
        const float sx=float(-dx),sz=float(-dz),radius=.42f;
        const RGB bank=tileColor(map,roads,store,across,
          mode==OverlayMode::Transit ? OverlayMode::Zone : mode,facilities,heat,penalty);
        constexpr int segments=8;
        for(int step=0;step<segments;++step) {
          const float a=float(step)*1.57079633f/segments,b=float(step+1)*1.57079633f/segments;
          const auto arc=[&](float angle,float r,float y) -> WorldPoint {
            return {cx+sx*(radius-r*std::cos(angle)),y,cz+sz*(radius-r*std::sin(angle))};
          };
          triangle(l,c,{{{cx,0,cz},arc(a,radius,0),arc(b,radius,0)}},bank);
          // A narrow sandy lip makes the curved waterline legible.
          quad(l,c,{{arc(a,radius,0),arc(a,radius-.035f,-.018f),
                      arc(b,radius-.035f,-.018f),arc(b,radius,0)}},
               mode==OverlayMode::Zone ? RGB{193,184,144} : bank);
        }
      }
      slab(l,c,x+.15f,z+.35f,.4f,.025f,-.02f,{133,185,213});
      continue;
    }
    if(tile.hasRoad && roads.hasLegacyRoadAt(t)) {
      bool arterial=false;
      for(Coord d:{Coord{1,0},Coord{-1,0},Coord{0,1},Coord{0,-1}}) {
        Coord n{t.x+d.x,t.y+d.y};
        if(roads.hasRoad(t,n)) arterial |= roads.getRoadClass(t,n)==RoadClass::Arterial;
      }
      const float width=arterial ? .76f : .55f, margin=(1-width)*.5f;
      const RGB asphalt=(mode==OverlayMode::Zone || mode==OverlayMode::Transit) ? RGB{57,63,70} : ground;
      const RGB stripe{223,202,133};
      const auto curve=roadCurve(map,&roads,t);
      if(curve.valid) {
        const auto ribbon=[&](float w,float offset,float y,RGB color) {
          curve.ribbon(w,offset,[&](const std::array<glm::vec2,4>& points) {
            std::array<WorldPoint,4> world;
            for(size_t i=0;i<4;++i) world[i]={x+points[i].x,y,z+points[i].y};
            quad(l,c,world,color);
          });
        };
        ribbon(width,0,.012f,asphalt);
        if(arterial) {ribbon(.025f,-.045f,.016f,stripe);ribbon(.025f,.045f,.016f,stripe);}
        else ribbon(.025f,0,.016f,stripe);
      } else {
        slab(l,c,x+margin,z+margin,width,width,.012f,asphalt);
        for(Coord d:{Coord{1,0},Coord{-1,0},Coord{0,1},Coord{0,-1}}) {
          const Coord n{t.x+d.x,t.y+d.y};
          if(!roads.hasRoad(t,n)) continue;
          if(d.x) {
            slab(l,c,x+(d.x>0 ? .5f : 0.f),z+margin,.5f,width,.012f,asphalt);
            slab(l,c,x+(d.x>0 ? .65f : .1f),z+.49f,.23f,.025f,.016f,stripe);
          } else {
            slab(l,c,x+margin,z+(d.y>0 ? .5f : 0.f),width,.5f,.012f,asphalt);
            slab(l,c,x+.49f,z+(d.y>0 ? .65f : .1f),.025f,.23f,.016f,stripe);
          }
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
  for(const auto& alignment:roads.alignments()) {
    const auto anchor=alignment.path.front();
    const RGB asphalt=(mode==OverlayMode::Zone || mode==OverlayMode::Transit) ? RGB{57,63,70} :
      tileColor(map,roads,store,anchor,mode,facilities,heat,penalty);
    drawAlignment3D(l,c,alignment,asphalt,false,&roads);
  }
  // Draw entire sites after the ground: footprint corners may be offscreen and
  // water parcels must not hide a constructed waterfront terminal.
  for(const auto& f:facilities) {PickParcel site(l,f.position);serviceSite(l,c,f,&map);}
  if(terminals) for(const auto& terminal:*terminals) {PickParcel site(l,terminal.origin);terminalSite(l,c,terminal);}
}
} // namespace visualizer
