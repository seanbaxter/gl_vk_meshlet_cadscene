#include "shaders_common.hxx"
#include "common.h"

struct Vertex {
  vec3  bboxCtr;
  vec3  bboxDim;
  vec3  coneNormal;
  float coneAngle;
  uint  meshletID;
};


[[spirv::vert]]
void bbox_vert() {
  uint meshletID = glvert_VertexIndex;
  uvec4 meshlet = meshletDescs[meshletID + push.geometryOffsets.x];

  vec3 bboxMin;
  vec3 bboxMax;
  decodeBbox(meshlet, object, bboxMin, bboxMax);
  
  vec3 ctr = (bboxMax + bboxMin) * 0.5f;
  vec3 dim = (bboxMax - bboxMin) * 0.5f;

  Vertex vertex { ctr, dim };
  decodeNormalAngle(meshlet, object, vertex.coneNormal, vertex.coneAngle); 
  vertex.meshletID = earlyCull(meshlet, object) ? ~0u : meshletID;

  shader_out<0, Vertex> = vertex;
}

// Each triangle strip is 4 vertices.
// The normal is 1 triangle strip.
// The bounding box is 6 triangle strips.
template<bool show_normal, bool show_box>
[[using spirv:
  geom(points, triangle_strip, 4), 
  invocations((int)show_normal + 6 * (int)show_box)
]]
void bbox_geom() {

}

[[spirv::frag]]
void bbox_frag() {

}

const bbox_shaders_t bbox_shaders {
  __spirv_data,
  __spirv_size,

  @spirv(bbox_vert),
  @spirv(bbox_geom<false, false>),
  @spirv(bbox_geom<false, true>),
  @spirv(bbox_geom<true, false>),
  @spirv(bbox_geom<true, true>),
  @spirv(bbox_frag)
};