#include "shaders_common.hxx"

[[spirv::vert]]
void bbox_vert() {
  uint meshletID = glvert_VertexIndex;
  uvec4 geometryOffsets = shader_push<uvec4>;
  uvec4 meshlet = meshletDescs[meshletID + geometryOffsets.x];

  vec3 bboxMin;
  vec3 bboxMax;
  decodeBbox(meshlet, object, bboxMin, bboxMax);
  
  vec3 ctr = (bboxMax + bboxMin) * 0.5f;
  vec3 dim = (bboxMax - bboxMin) * 0.5f;

  Vertex vertex { ctr, dim };
  decodeNormalAngle(meshlet, object, vertex.coneNormal, vertex.coneAngle); 
  vertex.meshletID = earlyCull(meshlet, object) ? -1 : meshletID;

  shader_out<0, Vertex> = vertex;
}

[[spirv::frag]]
void bbox_frag() {
  uint meshletID = shader_in<0, uint>;
  uint cp = murmurHash(meshletID);
  shader_out<0, vec4> = unpackUnorm4x8(cp);
}

const bbox_shaders_t bbox_shaders {
  __spirv_data,
  __spirv_size,

  @spirv(bbox_vert),
  0, //@spirv(bbox_geom),
  @spirv(bbox_frag)
};