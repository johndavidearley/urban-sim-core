#include "src/visualization/BuildingModels3D.hpp"
#include "src/visualization/Scene3DPrimitives.hpp"
#include <array>

namespace visualizer {
namespace {
using namespace scene3d;
uint32_t parcelHash(const Building& b) {
  uint32_t hash=uint32_t(b.position.x)*0x9e3779b9u ^ uint32_t(b.position.y)*0x85ebca6bu;
  hash^=hash>>16;hash*=0x7feb352du;
  return hash^(hash>>15);
}
void windows(DrawList* l,const Camera3D& c,float x,float z,float w,float d,
             float base,float height,RGB glass) {
  if(c.scale<10) return;
  for(float y=base+.12f;y<base+height-.06f;y+=.22f) {
    const float top=std::min(y+.07f,base+height-.02f);
    for(float side:{.2f,.56f}) {
      const float a=x+w*side,b=x+w*(side+.23f);
      quad(l,c,{{{a,y,z-.001f},{b,y,z-.001f},{b,top,z-.001f},{a,top,z-.001f}}},glass);
      quad(l,c,{{{a,y,z+d+.001f},{b,y,z+d+.001f},{b,top,z+d+.001f},{a,top,z+d+.001f}}},glass);
      const float u=z+d*side,v=z+d*(side+.23f);
      quad(l,c,{{{x-.001f,y,u},{x-.001f,y,v},{x-.001f,top,v},{x-.001f,top,u}}},glass);
      quad(l,c,{{{x+w+.001f,y,u},{x+w+.001f,y,v},{x+w+.001f,top,v},{x+w+.001f,top,u}}},glass);
    }
  }
}
void gable(DrawList* l,const Camera3D& c,float x,float z,float w,float d,
           float base,float rise,RGB roof,RGB end,bool rotate=false) {
  const auto p=[&](float u,float y,float v) -> WorldPoint {
    return rotate ? WorldPoint{x+v*w,y,z+u*d} : WorldPoint{x+u*w,y,z+v*d};
  };
  quad(l,c,{{p(0,base,0),p(.5f,base+rise,0),p(.5f,base+rise,1),p(0,base,1)}},shade(roof,.86f));
  quad(l,c,{{p(.5f,base+rise,0),p(1,base,0),p(1,base,1),p(.5f,base+rise,1)}},roof);
  triangle(l,c,{{p(0,base,0),p(1,base,0),p(.5f,base+rise,0)}},shade(end,.7f));
  triangle(l,c,{{p(1,base,1),p(0,base,1),p(.5f,base+rise,1)}},shade(end,.9f));
}
void dome(DrawList* l,const Camera3D& c,float x,float z,float radius,float base,float height,RGB color) {
  constexpr float pi=3.14159265359f;
  const auto p=[&](int ring,int segment) -> WorldPoint {
    const float a=segment*pi/6.f,b=ring*pi/8.f;
    return {x+radius*std::cos(b)*std::cos(a),base+height*std::sin(b),z+radius*std::cos(b)*std::sin(a)};
  };
  for(int ring=0;ring<4;++ring) for(int segment=0;segment<12;++segment) {
    const RGB facet=shade(color,.72f+.025f*float((segment+3)%10));
    if(ring==3) triangle(l,c,{{p(ring,segment),p(ring,segment+1),{x,base+height,z}}},facet);
    else quad(l,c,{{p(ring,segment),p(ring,segment+1),p(ring+1,segment+1),p(ring+1,segment)}},facet);
  }
}
void cross(DrawList* l,const Camera3D& c,float x,float z,float y,RGB color) {
  box(l,c,x+.43f,z+.24f,.14f,.52f,y,.025f,color);
  box(l,c,x+.24f,z+.43f,.52f,.14f,y,.026f,color);
}
void uniqueModel(DrawList* l,const Camera3D& c,const Building& b,RGB facade,RGB trim,RGB glass,RGB roof) {
  const float x=float(b.position.x),z=float(b.position.y);
  slab(l,c,x+.06f,z+.06f,.88f,.88f,.026f,trim);
  switch(b.uniqueKind) {
    case 1: // Treasury Hall: steps, colonnade and pediment.
      for(int i=0;i<3;++i) box(l,c,x+.1f+i*.035f,z+.1f+i*.035f,.8f-i*.07f,.8f-i*.07f,.027f+i*.04f,.04f,trim);
      box(l,c,x+.18f,z+.15f,.64f,.45f,.147f,.46f,facade);
      for(float dx:{.2f,.34f,.48f,.62f,.76f}) box(l,c,x+dx,z+.72f,.055f,.07f,.147f,.46f,trim);
      gable(l,c,x+.1f,z+.09f,.8f,.81f,.607f,.22f,roof,facade);
      break;
    case 2: // Transit Exchange: twin platforms and tall station clock.
      for(float dz:{.16f,.57f}) {
        box(l,c,x+.12f,z+dz,.76f,.27f,.027f,.12f,facade);
        for(float dx:{.2f,.75f}) box(l,c,x+dx,z+dz+.1f,.045f,.045f,.147f,.32f,trim);
        gable(l,c,x+.1f,z+dz-.02f,.8f,.31f,.467f,.13f,roof,facade);
      }
      box(l,c,x+.43f,z+.4f,.14f,.14f,.027f,.77f,facade);
      box(l,c,x+.4f,z+.37f,.2f,.2f,.797f,.09f,trim);
      break;
    case 3: // Trade Hall: market sheds and loading apron.
      for(float dx:{.12f,.51f}) {
        box(l,c,x+dx,z+.14f,.34f,.6f,.027f,.37f,facade);
        gable(l,c,x+dx-.02f,z+.12f,.38f,.64f,.397f,.17f,roof,facade);
      }
      for(float dx:{.17f,.46f,.73f}) box(l,c,x+dx,z+.81f,.13f,.1f,.027f,.13f,trim);
      break;
    case 4: // Visitor Pavilion: open canopy and information kiosk.
      for(float dx:{.17f,.76f}) for(float dz:{.17f,.76f}) box(l,c,x+dx,z+dz,.065f,.065f,.027f,.46f,facade);
      dome(l,c,x+.5f,z+.5f,.41f,.487f,.28f,roof);
      box(l,c,x+.34f,z+.35f,.32f,.26f,.027f,.22f,facade);
      box(l,c,x+.45f,z+.63f,.1f,.045f,.027f,.3f,glass);
      break;
    case 5: // Academy: teaching wings and clock tower.
      for(float dx:{.11f,.65f}) {
        box(l,c,x+dx,z+.18f,.24f,.65f,.027f,.42f,facade);
        windows(l,c,x+dx,z+.18f,.24f,.65f,.027f,.42f,glass);
        gable(l,c,x+dx-.015f,z+.16f,.27f,.69f,.447f,.14f,roof,facade);
      }
      box(l,c,x+.4f,z+.25f,.2f,.49f,.027f,.76f,facade);
      box(l,c,x+.435f,z+.745f,.13f,.015f,.61f,.1f,trim);
      gable(l,c,x+.36f,z+.22f,.28f,.55f,.787f,.2f,roof,facade);
      break;
    case 6: // Civic Square: fountain, statue and four benches.
      box(l,c,x+.2f,z+.2f,.6f,.6f,.027f,.05f,facade);
      slab(l,c,x+.25f,z+.25f,.5f,.5f,.078f,glass);
      box(l,c,x+.43f,z+.43f,.14f,.14f,.079f,.26f,trim);
      box(l,c,x+.47f,z+.47f,.06f,.06f,.339f,.22f,facade);
      for(float dz:{.12f,.83f}) for(float dx:{.14f,.62f}) box(l,c,x+dx,z+dz,.24f,.045f,.027f,.07f,roof);
      break;
    default: box(l,c,x+.16f,z+.16f,.68f,.68f,.027f,.6f,facade);break;
  }
}
void monumentModel(DrawList* l,const Camera3D& c,const Building& b,RGB facade,RGB trim,RGB glass,RGB accent) {
  const float x=float(b.position.x),z=float(b.position.y);
  box(l,c,x+.07f,z+.07f,.86f,.86f,.027f,.12f,trim);
  switch(b.monumentKind) {
    case 1: // Medical Citadel: three clinical towers and rooftop cross.
      for(float dx:{.14f,.66f}) {
        box(l,c,x+dx,z+.16f,.2f,.68f,.147f,.85f,facade);
        windows(l,c,x+dx,z+.16f,.2f,.68f,.147f,.85f,glass);
      }
      box(l,c,x+.37f,z+.25f,.26f,.5f,.147f,1.26f,facade);
      windows(l,c,x+.37f,z+.25f,.26f,.5f,.147f,1.26f,glass);
      cross(l,c,x,z,1.408f,accent);
      break;
    case 2: // Universal Campus: research wings and observatory.
      for(float dz:{.16f,.63f}) {
        box(l,c,x+.13f,z+dz,.74f,.21f,.147f,.48f,facade);
        windows(l,c,x+.13f,z+dz,.74f,.21f,.147f,.48f,glass);
      }
      box(l,c,x+.36f,z+.33f,.28f,.34f,.147f,.71f,facade);
      dome(l,c,x+.5f,z+.5f,.3f,.858f,.4f,glass);
      box(l,c,x+.47f,z+.47f,.06f,.06f,1.258f,.2f,trim);
      break;
    case 3: { // Clean Energy Spire: tapered tower and solar crown.
      quad(l,c,{{{x+.28f,.147f,z+.28f},{x+.72f,.147f,z+.28f},{x+.56f,2.1f,z+.44f},{x+.44f,2.1f,z+.44f}}},shade(facade,.65f));
      quad(l,c,{{{x+.72f,.147f,z+.28f},{x+.72f,.147f,z+.72f},{x+.56f,2.1f,z+.56f},{x+.56f,2.1f,z+.44f}}},shade(facade,.82f));
      quad(l,c,{{{x+.72f,.147f,z+.72f},{x+.28f,.147f,z+.72f},{x+.44f,2.1f,z+.56f},{x+.56f,2.1f,z+.56f}}},shade(facade,.9f));
      quad(l,c,{{{x+.28f,.147f,z+.72f},{x+.28f,.147f,z+.28f},{x+.44f,2.1f,z+.44f},{x+.44f,2.1f,z+.56f}}},shade(facade,.72f));
      box(l,c,x+.44f,z+.44f,.12f,.12f,2.1f,.14f,trim);
      box(l,c,x+.12f,z+.44f,.76f,.12f,1.52f,.045f,glass);
      box(l,c,x+.44f,z+.12f,.12f,.76f,1.52f,.045f,glass);
      break;
    }
    case 4: // Ecology Dome: faceted greenhouse on a planted base.
      slab(l,c,x+.12f,z+.12f,.76f,.76f,.148f,accent);
      dome(l,c,x+.5f,z+.5f,.39f,.15f,.7f,glass);
      box(l,c,x+.4f,z+.86f,.2f,.06f,.147f,.2f,trim);
      break;
    default: box(l,c,x+.18f,z+.18f,.64f,.64f,.147f,1.f,facade);break;
  }
}
} // namespace

void drawBuilding3D(DrawList* l,const Camera3D& c,const Building& b,RGB color,bool diagnostic) {
  using namespace scene3d;
  const uint32_t variation=parcelHash(b);
  const float x=float(b.position.x),z=float(b.position.y);
  const int tier=buildingDensityTier(b.type,b.capacity);
  const float height=.35f+.28f*tier;
  const RGB facade=shade(color,diagnostic ? 1.f : .91f+.025f*float(variation%4));
  const std::array<RGB,3> roofs{{{143,88,70},{88,118,120},{126,116,97}}};
  const RGB roof=diagnostic ? shade(facade,.8f) : roofs[(variation>>4)%roofs.size()];
  const RGB glass=diagnostic ? shade(facade,.65f) : RGB{73,117,146};
  const RGB trim=diagnostic ? shade(facade,.92f) : RGB{225,218,194};
  const RGB accent=diagnostic ? shade(facade,.85f) : RGB{101,162,124};
  if(b.monumentKind) {monumentModel(l,c,b,facade,trim,glass,accent);return;}
  if(b.uniqueKind) {uniqueModel(l,c,b,facade,trim,glass,roof);return;}
  slab(l,c,x+.08f,z+.08f,.84f,.84f,.026f,shade(trim,.9f));
  if(b.hotelBeds) {
    const float h=std::max(.8f,height);
    box(l,c,x+.17f,z+.17f,.66f,.66f,.027f,h,facade);
    windows(l,c,x+.17f,z+.17f,.66f,.66f,.027f,h,glass);
    box(l,c,x+.31f,z+.79f,.38f,.14f,.22f,.045f,trim);
    for(float dx:{.34f,.63f}) box(l,c,x+dx,z+.87f,.03f,.03f,.027f,.193f,trim);
    const RGB sign=b.hotelOpen ? trim : shade(facade,.65f);
    for(float dx:{.38f,.56f}) box(l,c,x+dx,z+.46f,.06f,.055f,h+.027f,.21f,sign);
    box(l,c,x+.44f,z+.46f,.12f,.055f,h+.11f,.045f,sign);
  } else if(b.type==BuildingType::Residential && tier<=1) {
    const float body=height-.13f;
    box(l,c,x+.18f,z+.18f,.64f,.64f,.027f,body,facade);
    windows(l,c,x+.18f,z+.18f,.64f,.64f,.027f,body,glass);
    gable(l,c,x+.12f,z+.12f,.76f,.76f,body+.027f,.13f,roof,facade,(variation&1)!=0);
    box(l,c,x+.45f,z+.822f,.11f,.013f,.027f,std::min(.18f,body),roof);
    slab(l,c,x+.44f,z+.84f,.14f,.1f,.029f,trim);
  } else if(b.type==BuildingType::Residential) {
    box(l,c,x+.17f,z+.17f,.66f,.66f,.027f,height,facade);
    windows(l,c,x+.17f,z+.17f,.66f,.66f,.027f,height,glass);
    if(c.scale>=10) for(float y=.25f;y<height-.05f;y+=.28f) {
      for(float dz:{.12f,.82f}) box(l,c,x+.26f,z+dz,.48f,.06f,y,.035f,trim);
      for(float dx:{.12f,.82f}) box(l,c,x+dx,z+.26f,.06f,.48f,y,.035f,trim);
    }
    box(l,c,x+.15f,z+.15f,.7f,.7f,height+.027f,.045f,roof);
  } else if(b.type==BuildingType::Commercial) {
    box(l,c,x+.14f,z+.17f,.72f,.66f,.027f,height,facade);
    windows(l,c,x+.14f,z+.17f,.72f,.66f,.027f,height,glass);
    box(l,c,x+.23f,z+.832f,.54f,.012f,.07f,.16f,glass);
    const float awning=std::min(.3f,height*.75f);
    for(int stripe=0;stripe<6;++stripe)
      quad(l,c,{{{x+.14f+stripe*.12f,awning,z+.82f},{x+.26f+stripe*.12f,awning,z+.82f},
                 {x+.26f+stripe*.12f,awning-.04f,z+.94f},{x+.14f+stripe*.12f,awning-.04f,z+.94f}}},stripe%2 ? trim : roof);
    box(l,c,x+.28f,z+.4f,.44f,.1f,height+.027f,.09f,roof);
  } else if(b.type==BuildingType::Industrial) {
    const float body=height*.7f;
    box(l,c,x+.14f,z+.14f,.72f,.72f,.027f,body,facade);
    for(float dx:{.12f,.5f}) gable(l,c,x+dx,z+.12f,.38f,.76f,body+.027f,height*.18f,roof,facade);
    box(l,c,x+.73f,z+.22f,.1f,.12f,body+.027f,height*.5f,trim);
    box(l,c,x+.73f,z+.22f,.1f,.12f,height*1.2f+.027f,.04f,roof);
    for(float dx:{.23f,.53f}) box(l,c,x+dx,z+.861f,.2f,.012f,.027f,body*.7f,shade(roof,.8f));
  } else { // Office: glazed tower, podium and setback roof.
    const float podium=std::min(.22f,height*.3f), margin=tier>=3 ? .23f : .16f;
    box(l,c,x+.13f,z+.13f,.74f,.74f,.027f,podium,facade);
    box(l,c,x+margin,z+margin,1-2*margin,1-2*margin,podium+.027f,height-podium,facade);
    windows(l,c,x+margin,z+margin,1-2*margin,1-2*margin,podium+.027f,height-podium,glass);
    box(l,c,x+.32f,z+.32f,.36f,.36f,height+.027f,.08f,glass);
    box(l,c,x+.48f,z+.48f,.035f,.035f,height+.107f,.13f,trim);
  }
}
} // namespace visualizer
