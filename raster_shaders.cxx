// Use the existing struct definitions, but this time the __cplusplus 
// code path even for shaders.
#include "shaders_common.hxx"

template<bool clip_primitives>
[[spirv::vert]]
void vert_shader() {
  vec3 oPos = shader_in<VERTEX_POS, vec3>;
  vec3 oNormal = shader_in<VERTEX_NORMAL, vec3>;

  vec3 wPos = (object.worldMatrix * vec4(oPos, 1)).xyz;
  glvert_Output.Position = (scene.viewProjMatrix * vec4(wPos, 1));

  if constexpr(clip_primitives) {
    glvert_Output.ClipDistance[0] = dot(scene.wClipPlanes[0], vec4(wPos, 1));
    glvert_Output.ClipDistance[1] = dot(scene.wClipPlanes[1], vec4(wPos, 1));
    glvert_Output.ClipDistance[2] = dot(scene.wClipPlanes[2], vec4(wPos, 1));
  }
  vec3 wNormal = mat3(object.worldMatrixIT) * oNormal;

  Vertex vertex { };
  vertex.pos = wPos;
  vertex.dummy = 0;
  vertex.normal = wNormal;
  vertex.meshletID = 0;
  shader_out<0, Vertex> = vertex;
}

[[spirv::frag]]
void frag_shader() {
  Vertex vertex = shader_in<0, Vertex>;

  vec4 color = object.color * .8f + .2f + vertex.dummy;
  if(scene.colorize) {
    uint colorPacked = murmurHash(vertex.meshletID);
    color = 0.5f * (color + unpackUnorm4x8(colorPacked));
  }

  vec3 eyePos(
    scene.viewMatrixIT[0].w, 
    scene.viewMatrixIT[1].w, 
    scene.viewMatrixIT[2].w
  );


  vec3 lightDir = normalize(scene.wLightPos.xyz - vertex.pos);
  vec3 normal = normalize(vertex.normal);

  vec4 diffuse = vec4(abs(dot(normal, lightDir)));
  color *= diffuse;
  
  shader_out<0, vec4> = color;
}

const raster_shaders_t raster_shaders {
  __spirv_data,
  __spirv_size,
  @spirv(vert_shader<false>),
  @spirv(vert_shader<true>),
  @spirv(frag_shader),
};