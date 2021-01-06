#include "shaders_common.hxx"

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

  faceNormal = mat3(worldTM) * faceNormal;
  edgeBasis0 = mat3(worldTM) * edgeBasis0;
  edgeBasis1 = mat3(worldTM) * edgeBasis1;

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

const bbox_shaders_t bbox_shaders2 {
  __spirv_data,
  __spirv_size,

  0,
  @spirv(bbox_geom),
  0,
};