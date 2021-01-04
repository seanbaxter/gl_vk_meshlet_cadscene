// Use the existing struct definitions, but this time the __cplusplus 
// code path even for shaders.
#include "shaders_common.hxx"

enum typename interpolants_t {
  interpolant_pos = vec3,
  interpolate_dummy = float,
  interpolant_normal = vec3,
  interpolant_meshletID = int,
  interpolant_extra = vec4
};

template<int extra_attributes>
[[spirv::vert]]
void vert_shader() {
  vec3 oPos = shader_in<VERTEX_POS, vec3>;
  vec3 oNormal = shader_in<VERTEX_NORMAL, vec3>;

  vec3 wPos = (object.worldMatrix * vec4(oPos, 1)).xyz;
  glvert_Output.Position = (scene.viewProjMatrix * vec4(wPos, 1));

  shader_out<interpolant_pos> = wPos;
  shader_out<interpolate_dummy> = 0;

  // TODO: Perform clipping.

  vec3 wNormal = mat3(object.worldMatrixIT) * oNormal;
  shader_out<interpolant_normal> = wNormal;

  shader_out<interpolant_meshletID> = 0;

  // Forward the extra attributes.
  @meta for(int i = 0; i < extra_attributes; ++i)
    shader_out<(int)interpolant_extra + i, vec4> =  
      shader_in<VERTEX_XTRA + i, vec4>;
}

template<int extra_attributes>
[[spirv::frag]]
void frag_shader() {
  vec4 color = object.color * .8f + .2f + shader_in<interpolate_dummy>;
  if(scene.colorize) {
    uint colorPacked = murmurHash(shader_in<interpolant_meshletID>);
    color = 0.5f * (color + unpackUnorm4x8(colorPacked));
  }

  vec3 eyePos(
    scene.viewMatrixIT[0].w, 
    scene.viewMatrixIT[1].w, 
    scene.viewMatrixIT[2].w
  );

  vec3 wNormal = shader_in<interpolant_normal>;
  vec3 wPos = shader_in<interpolant_pos>;
  vec3 lightDir = normalize(scene.wLightPos.xyz - wPos);
  vec3 normal = normalize(wNormal);

  vec4 diffuse = vec4(abs(dot(normal, lightDir)));
  color *= diffuse;

  // Add extra attributes contributions.
  @meta for(int i = 0; i < extra_attributes; ++i)
    color += shader_in<(int)interpolant_extra + i, vec4>;

  shader_out<0, vec4> = color;
}

const raster_shaders_t raster_shaders {
  __spirv_data,
  __spirv_size,
  @spirv(vert_shader<0>),
  @spirv(frag_shader<0>),
  @spirv(vert_shader<1>),
  @spirv(frag_shader<1>),
  @spirv(vert_shader<2>),
  @spirv(frag_shader<2>),
  @spirv(vert_shader<3>),
  @spirv(frag_shader<3>),
  @spirv(vert_shader<4>),
  @spirv(frag_shader<4>),
};