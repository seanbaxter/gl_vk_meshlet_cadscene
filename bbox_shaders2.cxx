#include "shaders_common.hxx"

// The normal is 1 triangle strip.
// The bounding box is 6 triangle strips.
[[using spirv:
  geom(points, triangle_strip, 4), 
  invocations(6)
]]
void bbox_geom() {
  // The input attribute is a Vertex[1], because the input type is points.
  // This would have a different dimension for edges or triangles.
  Vertex vertex = shader_in<0, Vertex[1]>[0];

  if(-1 == vertex.meshletID)
    return;

  mat4 worldTM = object.worldMatrix;
  vec3 worldCtr = (worldTM * vec4(vertex.bboxCtr, 1)).xyz;

  // Each invocation draws one face.
  vec3 faceNormal { }, edgeBasis0 { }, edgeBasis1 { };
  switch(glgeom_InvocationID % 3) {
    case 0:
      faceNormal.x = vertex.bboxDim.x;
      edgeBasis0.y = vertex.bboxDim.y;
      edgeBasis1.z = vertex.bboxDim.z;
      break;

    case 1:
      faceNormal.y = vertex.bboxDim.y;
      edgeBasis1.x = vertex.bboxDim.x;
      edgeBasis0.z = vertex.bboxDim.z;
      break;

    case 2:
      faceNormal.z = vertex.bboxDim.z;
      edgeBasis0.x = vertex.bboxDim.x;
      edgeBasis1.y = vertex.bboxDim.y;
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
  
  shader_out<0, uint> = vertex.meshletID;
  glgeom_Output.Position = scene.viewProjMatrix * 
    vec4(worldCtr - edgeBasis0 - edgeBasis1, 1);
  glgeom_EmitVertex();
  
  shader_out<0, uint> = vertex.meshletID;
  glgeom_Output.Position = scene.viewProjMatrix * 
    vec4(worldCtr + edgeBasis0 - edgeBasis1, 1);
  glgeom_EmitVertex();
  
  shader_out<0, uint> = vertex.meshletID;
  glgeom_Output.Position = scene.viewProjMatrix * 
    vec4(worldCtr - edgeBasis0 + edgeBasis1, 1);
  glgeom_EmitVertex();
  
  shader_out<0, uint> = vertex.meshletID;
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