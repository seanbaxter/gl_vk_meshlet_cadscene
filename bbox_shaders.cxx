#include "shaders.hxx"

// Each triangle strip is 4 vertices.
// The normal is 1 triangle strip.
// The bounding box is 6 triangle strips.
template<bool show_normal, bool show_box>
[[using spirv:
  geom(points, triangle_strip, 4), 
  invocations((int)show_normal + 6 * (int)show_box)
]]
void geom_bbox() {

}

