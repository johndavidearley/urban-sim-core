#include "src/visualization/Scene3D.hpp"
#include "src/visualization/VisualizerOverlay.hpp"
#include "src/systems/ParkSystem.hpp"
#include <array>

namespace visualizer {
namespace {
RGB shade(RGB c,float amount) {
  return {static_cast<uint8_t>(c.r*amount),static_cast<uint8_t>(c.g*amount),static_cast<uint8_t>(c.b*amount)};
}
void quad(DrawList* list,const Camera3D& camera,std::array<WorldPoint,4> points,RGB color,uint8_t alpha=255,bool highlight=false) {
  std::array<ProjectedPoint,4> p;
  for (size_t i=0;i<4;++i) p[i]=camera.project(points[i]);
  const auto begin=list->vertices.size();
  setDrawColor(list,color.r,color.g,color.b,alpha);
  list->triangle(p[0].x,p[0].y,p[1].x,p[1].y,p[2].x,p[2].y);
  list->triangle(p[0].x,p[0].y,p[2].x,p[2].y,p[3].x,p[3].y);
  const int indices[]={0,1,2,0,2,3};
  for(size_t i=0;i<6;++i) list->vertices[begin+i].z=highlight ? 0.f : p[indices[i]].depth;
}
void slab(DrawList* l,const Camera3D& c,float x,float z,float w,float d,float y,RGB color) {
  quad(l,c,{{{x,y,z},{x+w,y,z},{x+w,y,z+d},{x,y,z+d}}},color);
}
void box(DrawList* l,const Camera3D& c,float x,float z,float w,float d,float base,float h,RGB color) {
  const float y=base+h;
  quad(l,c,{{{x,base,z},{x+w,base,z},{x+w,y,z},{x,y,z}}},shade(color,.65f));
  quad(l,c,{{{x+w,base,z},{x+w,base,z+d},{x+w,y,z+d},{x+w,y,z}}},shade(color,.82f));
  quad(l,c,{{{x+w,base,z+d},{x,base,z+d},{x,y,z+d},{x+w,y,z+d}}},shade(color,.9f));
  quad(l,c,{{{x,base,z+d},{x,base,z},{x,y,z},{x,y,z+d}}},shade(color,.72f));
  slab(l,c,x,z,w,d,y,color);
}
}
void drawGroundHighlight(DrawList* list,const Camera3D& c,Coord t,RGB color,uint8_t alpha) {
  quad(list,c,{{{float(t.x),.04f,float(t.y)},{t.x+1.f,.04f,float(t.y)},
               {t.x+1.f,.04f,t.y+1.f},{float(t.x),.04f,t.y+1.f}}},color,alpha,true);
}
void drawCity3D(DrawList* l,const Camera3D& c,const CityMap& map,const RoadNetwork& roads,
                const EntityStore& store,const std::vector<ServiceFacility>& facilities,OverlayMode mode,
                const std::unordered_map<Coord,float,Vec2Hash>& heat,float penalty,
                const DistrictSystem& districts,DistrictId selectedDistrict,const TransitMarks* transit) {
  const auto size=map.getDimensions();
  // A solid plinth gives the flat simulation map a visible edge from every angle.
  box(l,c,0,0,float(size.x),float(size.y),-.35f,.34f,{91,108,83});
  std::unordered_map<Coord,const ServiceFacility*,Vec2Hash> sites;
  for(const auto& f:facilities) sites[f.position]=&f;
  for(int z=0;z<size.y;++z) for(int x=0;x<size.x;++x) {
    const Coord t{x,z};const auto& tile=map.getTile(t);
    const auto center=c.project({x+.5f,0,z+.5f});
    // Keep offscreen towers: their roof can extend several tiles above the ground.
    if(center.x < -c.scale*8 || center.x > l->viewportWidth+c.scale*8 ||
       center.y < -c.scale*2 || center.y > l->viewportHeight+c.scale*12) continue;
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
    if(tile.regionalConnectionId) {
      slab(l,c,x+.05f,z+.05f,.9f,.9f,.02f,{114,133,148});
      box(l,c,x+.18f,z+.2f,.64f,.45f,.02f,.18f,{160,183,195});
    }
    const auto* b=store.getBuilding(tile.buildingId);
    if(b) {
      const int tier=buildingDensityTier(b->type,b->capacity);
      const float height=.35f+.28f*tier+(b->monumentKind ? 1.4f : b->uniqueKind ? .65f : 0.f);
      RGB facade=b->monumentKind ? RGB{126,180,190} : b->uniqueKind ? RGB{207,173,102} :
        b->type==BuildingType::Residential ? RGB{220,189,151} :
        b->type==BuildingType::Commercial ? RGB{109,164,190} :
        b->type==BuildingType::Office ? RGB{143,159,194} : RGB{178,148,108};
      if(mode!=OverlayMode::Zone && mode!=OverlayMode::Transit) facade=ground;
      box(l,c,x+.13f,z+.13f,.74f,.74f,.025f,height,facade);
      box(l,c,x+.24f,z+.24f,.52f,.52f,height+.026f,.06f,shade(facade,.87f));
      // Windows occupy all four facades, so they remain visible while orbiting.
      if(c.scale>=10) for(float y=.18f;y<height-.08f;y+=.22f) {
        const RGB glass{70,103,123};
        quad(l,c,{{{x+.25f,y,z+.129f},{x+.75f,y,z+.129f},{x+.75f,y+.07f,z+.129f},{x+.25f,y+.07f,z+.129f}}},glass);
        quad(l,c,{{{x+.25f,y,z+.871f},{x+.75f,y,z+.871f},{x+.75f,y+.07f,z+.871f},{x+.25f,y+.07f,z+.871f}}},glass);
        quad(l,c,{{{x+.129f,y,z+.25f},{x+.129f,y,z+.75f},{x+.129f,y+.07f,z+.75f},{x+.129f,y+.07f,z+.25f}}},glass);
        quad(l,c,{{{x+.871f,y,z+.25f},{x+.871f,y,z+.75f},{x+.871f,y+.07f,z+.75f},{x+.871f,y+.07f,z+.25f}}},glass);
      }
    }
    const auto facility=sites.find(t);
    if(facility!=sites.end()) {
      const RGB color=serviceFacilityColor(facility->second->type);
      box(l,c,x+.17f,z+.17f,.66f,.66f,.025f,.5f,color);
      box(l,c,x+.4f,z+.4f,.2f,.2f,.526f,.18f,{235,235,216});
    }
    if(ParkSystem::isPark(map,t) || (!tile.hasRoad&&!tile.buildingId&&!tile.regionalConnectionId&&tile.zone==0&&tile.type==0&&((x*31+z*17)%19==0))) {
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
}
} // namespace visualizer
