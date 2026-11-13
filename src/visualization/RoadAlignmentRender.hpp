#pragma once
#include "src/visualization/Scene3DPrimitives.hpp"

namespace visualizer {
// One tessellator feeds both projections and the construction preview.
// Each emitted quad carries the centerline parcel for geometry picking.
template<class Emit>
void alignmentRibbon(const RoadAlignment& a,float width,float offset,Emit emit) {
  for(size_t curveIndex=0;curveIndex<a.curves.size();++curveIndex) {
    const auto& curve=a.curves[curveIndex];
    const auto normal=[&](float t) {
      auto d=curve.tangent(t);
      float length=std::sqrt(d.x*d.x+d.y*d.y);
      if(length<.00001f) {
        d=curve.point(std::min(1.f,t+.001f))-curve.point(std::max(0.f,t-.001f));
        length=std::sqrt(d.x*d.x+d.y*d.y);
      }
      return length>.000001f ? glm::vec2{-d.y/length,d.x/length} : glm::vec2{0,1};
    };
    const int steps=curve.subdivisions();
    for(int i=0;i<steps;++i) {
      const float t=float(i)/steps,u=float(i+1)/steps;
      const auto p=curve.point(t),q=curve.point(u),n=normal(t),m=normal(u);
      emit(std::array<glm::vec2,4>{{p+n*(offset-width*.5f),q+m*(offset-width*.5f),
                                   q+m*(offset+width*.5f),p+n*(offset+width*.5f)}},
           Coord{int(std::floor((p.x+q.x)*.5f)),int(std::floor((p.y+q.y)*.5f))},
           std::array<float,4>{{a.height((float(curveIndex)+t)/a.curves.size()),a.height((float(curveIndex)+u)/a.curves.size()),
                                 a.height((float(curveIndex)+u)/a.curves.size()),a.height((float(curveIndex)+t)/a.curves.size())}});
    }
  }
}
inline void drawAlignment3D(DrawList* list,const Camera3D& camera,const RoadAlignment& a,
                            RGB asphalt={57,63,70},bool preview=false,const RoadNetwork* roads=nullptr) {
  const auto ribbon=[&](float width,float offset,float height,RGB color) {
    alignmentRibbon(a,width,offset,[&](const std::array<glm::vec2,4>& points,Coord tile,const std::array<float,4>& profile) {
      // Open the guardrails at live deck merges; retained split points
      // without a ramp do not leave permanent holes in the highway rails.
      if(width==.035f && roads && a.elevation==1) {
        for(const auto& ramp:roads->alignments()) if(ramp.roadClass==RoadClass::Ramp && ramp.elevation==1) {
          for(bool first:{true,false}) if((first ? ramp.startLevel : ramp.endLevel)==1) {
            const auto p=first ? ramp.path.front() : ramp.path.back();
            if(std::abs(p.x-tile.x)<=1 && std::abs(p.y-tile.y)<=1 &&
               ((a.roadClass==RoadClass::Highway && a.hasDeckAt(p)) || &a==&ramp)) return;
          }
        }
      }
      scene3d::PickParcel parcel(list,tile);
      std::array<WorldPoint,4> world;
      for(size_t i=0;i<4;++i) world[i]={points[i].x,height+profile[i],points[i].y};
      scene3d::quad(list,camera,world,color,preview ? 180 : 255,preview);
      if(a.elevation!=0 && !preview && width==roadWidth(a.roadClass)) {
        auto bottom=world;
        for(size_t i=0;i<4;++i) bottom[i].y=a.elevation>0 ? world[i].y-.1f : -.94f;
        scene3d::quad(list,camera,{{world[0],world[1],bottom[1],bottom[0]}},{110,118,114});
        scene3d::quad(list,camera,{{world[2],world[3],bottom[3],bottom[2]}},{91,101,99});
        if(a.elevation>0) scene3d::quad(list,camera,bottom,{80,89,87});
      }
    });
  };
  const float width=roadWidth(a.roadClass);
  ribbon(width,0,.02f,asphalt);
  if(preview) return;
  if(a.elevation!=0) {
    ribbon(.035f,-width*.5f,.09f,{178,189,184});ribbon(.035f,width*.5f,.09f,{178,189,184});
  }
  if(a.elevation>0) for(const auto& curve:a.curves) for(float t:{.35f,.65f}) {
    const auto p=curve.point(t);const Coord tile{int(std::floor(p.x)),int(std::floor(p.y))};
    bool blocked=roads && roads->hasRoadAdjacency(tile);
    if(roads) for(const auto& other:roads->alignments())
      if(other.elevation<0 && std::find(other.footprint.begin(),other.footprint.end(),tile)!=other.footprint.end()) blocked=true;
    if(!blocked) {
      scene3d::PickParcel parcel(list,tile);
      scene3d::box(list,camera,p.x-.08f,p.y-.08f,.16f,.16f,0.f,a.height(t)-.08f,{162,173,166});
    }
  }
  const RGB white{225,229,216},yellow{225,198,94};
  if(a.roadClass==RoadClass::Highway) {
    ribbon(.025f,-width*.4f,.024f,white);ribbon(.025f,width*.4f,.024f,white);
    ribbon(.025f,-.035f,.025f,yellow);ribbon(.025f,.035f,.025f,yellow);
  } else if(!a.oneWay) ribbon(.025f,0,.024f,yellow);
  if(a.oneWay) {
    for(const auto& curve:a.curves) for(float t:{.25f,.75f}) {
      const auto p=curve.point(t),d=curve.tangent(t);
      const float length=std::sqrt(d.x*d.x+d.y*d.y);
      if(length<.001f) continue;
      const auto along=d*(1.f/length),side=glm::vec2{-along.y,along.x};
      const auto tip=p+along*.16f,left=p-along*.1f+side*.1f,right=p-along*.1f-side*.1f;
      scene3d::PickParcel parcel(list,{int(std::floor(p.x)),int(std::floor(p.y))});
      scene3d::triangle(list,camera,{{{tip.x,a.height(t)+.026f,tip.y},{left.x,a.height(t)+.026f,left.y},{right.x,a.height(t)+.026f,right.y}}},white);
    }
  }
}
inline void drawAlignmentTopDown(DrawList* list,const RoadAlignment& a,int viewX,int viewY,int scale,
                                 RGB asphalt={57,63,70},bool preview=false,const RoadNetwork* roads=nullptr) {
  const auto ribbon=[&](float width,float offset,RGB color) {
    setDrawColor(list,color.r,color.g,color.b,preview ? 180 : 255);
    alignmentRibbon(a,width,offset,[&](const std::array<glm::vec2,4>& p,Coord tile,const std::array<float,4>&) {
      if(a.elevation<0 && !preview && roads && roads->hasRoadAdjacency(tile) &&
         tile!=a.path.front() && tile!=a.path.back()) return;
      const auto screen=[&](glm::vec2 v) {return glm::vec2{(v.x-viewX)*scale,(v.y-viewY)*scale};};
      const auto x=screen(p[0]),y=screen(p[1]),z=screen(p[2]),w=screen(p[3]);
      list->triangle(x.x,x.y,y.x,y.y,z.x,z.y);list->triangle(x.x,x.y,z.x,z.y,w.x,w.y);
    });
  };
  const float width=roadWidth(a.roadClass);
  if(a.elevation>0 && !preview) ribbon(width+.12f,0,{111,124,119});
  ribbon(width,0,asphalt);
  if(preview) return;
  if(!a.oneWay) ribbon(.025f,0,{225,198,94});
  if(a.roadClass==RoadClass::Highway) {
    ribbon(.025f,-width*.4f,{225,229,216});ribbon(.025f,width*.4f,{225,229,216});
  }
  if(a.oneWay) for(const auto& curve:a.curves) for(float t:{.25f,.75f}) {
    const auto p=curve.point(t),d=curve.tangent(t);
    if(a.elevation<0 && roads && roads->hasRoadAdjacency({int(std::floor(p.x)),int(std::floor(p.y))})) continue;
    const float length=std::sqrt(d.x*d.x+d.y*d.y);
    if(length<.001f) continue;
    const auto along=d*(1.f/length),side=glm::vec2{-along.y,along.x};
    const auto tip=p+along*.16f,left=p-along*.1f+side*.1f,right=p-along*.1f-side*.1f;
    setDrawColor(list,225,229,216,255);
    list->triangle((tip.x-viewX)*scale,(tip.y-viewY)*scale,(left.x-viewX)*scale,(left.y-viewY)*scale,
      (right.x-viewX)*scale,(right.y-viewY)*scale);
  }
}
} // namespace visualizer
