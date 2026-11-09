#pragma once
#include "src/visualization/Camera3D.hpp"
#include "src/visualization/VisualizerTypes.hpp"

namespace visualizer {
// Rendering derives solely from persistent building metadata and parcel position.
// Diagnostic overlays keep the supplied color instead of architectural materials.
void drawBuilding3D(DrawList* list,const Camera3D& camera,const Building& building,
                    RGB color,bool diagnosticOverlay=false);
}
