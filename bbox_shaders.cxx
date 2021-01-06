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

  Box vertex { ctr, dim };
  decodeNormalAngle(meshlet, object, vertex.coneNormal, vertex.coneAngle); 
  vertex.meshletID = earlyCull(meshlet, object) ? -1 : meshletID;

  shader_out<0, Box> = vertex;
}

// Workaround for https://developer.nvidia.com/nvidia_bug/3222796.
// Find the geometry shader in bbox_shaders2.cxx
#ifdef ENABLE_GEOM
// The normal is 1 triangle strip.
// The bounding box is 6 triangle strips.
[[using spirv:
  geom(points, triangle_strip, 4), 
  invocations(6)
]]
void bbox_geom() {
  // The input attribute is a Box[1], because the input type is points.
  // This would have a different dimension for edges or triangles.
  Box box = shader_in<0, Box[1]>[0];

  if(-1 == box.meshletID)
    return;

  mat4 worldTM = object.worldMatrix;
  vec3 worldCtr = (worldTM * vec4(box.ctr, 1)).xyz;

  // Each invocation draws one face.
  vec3 faceNormal { }, edgeBasis0 { }, edgeBasis1 { };
  switch(glgeom_InvocationID % 3) {
    case 0:
      faceNormal.x = box.dim.x;
      edgeBasis0.y = box.dim.y;
      edgeBasis1.z = box.dim.z;
      break;

    case 1:
      faceNormal.y = box.dim.y;
      edgeBasis1.x = box.dim.x;
      edgeBasis0.z = box.dim.z;
      break;

    case 2:
      faceNormal.z = box.dim.z;
      edgeBasis0.x = box.dim.x;
      edgeBasis1.y = box.dim.y;
      break;
  }

  vec3 worldNormal = mat3(worldTM) * faceNormal;
  vec3 worldPos    = worldCtr + worldNormal;
  float proj = -sign(dot(worldPos - scene.viewPos.xyz, worldNormal));

  if(glgeom_InvocationID > 2)
    proj = -proj;

  faceNormal = mat3(worldTM) * (faceNormal);
  edgeBasis0 = mat3(worldTM) * (edgeBasis0);
  edgeBasis1 = mat3(worldTM) * (edgeBasis1);

  worldCtr += faceNormal;
  
  shader_out<0, uint> = box.meshletID;
  glgeom_Output.Position = scene.viewProjMatrix * 
    vec4(worldCtr - edgeBasis0 - edgeBasis1, 1);
  glgeom_EmitVertex();
  
  shader_out<0, uint> = box.meshletID;
  glgeom_Output.Position = scene.viewProjMatrix * 
    vec4(worldCtr + edgeBasis0 - edgeBasis1, 1);
  glgeom_EmitVertex();
  
  shader_out<0, uint> = box.meshletID;
  glgeom_Output.Position = scene.viewProjMatrix * 
    vec4(worldCtr - edgeBasis0 + edgeBasis1, 1);
  glgeom_EmitVertex();
  
  shader_out<0, uint> = box.meshletID;
  glgeom_Output.Position = scene.viewProjMatrix * 
    vec4(worldCtr + edgeBasis0 + edgeBasis1, 1);
  glgeom_EmitVertex();
}
#endif // ENABLE_GEOM

// NOTE: bbox_geom should go here. NVIDIA Driver Bug #xxx makes us put it in
// its own translation unit.

[[spirv::frag]]
void bbox_frag() {
  // Load in from the 
  uint meshletID = shader_in<0, uint>;
  uint cp = murmurHash(meshletID);
  shader_out<0, vec4> = unpackUnorm4x8(cp);
}

const bbox_shaders_t bbox_shaders {
  __spirv_data,
  __spirv_size,

  @spirv(bbox_vert),
#ifdef ENABLE_GEOM
  @spirv(bbox_geom),
#else
  nullptr,
#endif
  @spirv(bbox_frag)
};