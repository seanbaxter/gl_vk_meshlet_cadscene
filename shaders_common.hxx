#pragma once
#include "shaders.hxx"
#include "common.h"
#include <cstdint>

template<auto index, typename type_t = @enum_type(index)>
[[using spirv: in, location((int)index)]]
type_t shader_in;

template<auto index, typename type_t = @enum_type(index)>
[[using spirv: out, location((int)index)]]
type_t shader_out;

template<typename type_t>
[[using spirv: push]]
type_t shader_push;

[[using spirv: uniform, binding(SCENE_UBO_VIEW), set(DSET_SCENE)]]
SceneData scene;

[[using spirv: uniform, binding(0), set(DSET_OBJECT)]]
ObjectData object;

[[using spirv: buffer, binding(GEOMETRY_SSBO_MESHLETDESC), set(DSET_GEOMETRY)]]
uvec4 meshletDescs[];

[[spirv::push]]
struct {
  uvec4 geometryOffsets;
  uvec4 assigns;
} push;

struct Vertex {
  vec3  bboxCtr;
  vec3  bboxDim;
  vec3  coneNormal;
  float coneAngle;
  uint  meshletID;
};

constexpr int div_up(int x, int y) {
  return (x + y - 1) / y;
}

struct meshlet_t {
  uint vertMax, primMax, primStart, primDiv, vidxStart, vidxBits, vidxDiv;
};

inline meshlet_t decodeMeshlet(uvec4 meshletDesc) {
  meshlet_t meshlet { };

  uint vMax  = (meshletDesc.x >> 24);
  uint packOffset = meshletDesc.w;
  
  meshlet.vertMax    = vMax;
  meshlet.primMax    = (meshletDesc.y >> 24);
  
  meshlet.vidxStart  =  packOffset;
  meshlet.vidxDiv    = (meshletDesc.z >> 24);
  meshlet.vidxBits   = meshlet.vidxDiv == 2 ? 16 : 0;
  
  meshlet.primDiv    = 4;
  meshlet.primStart  = (packOffset + ((vMax + 1 + meshlet.vidxDiv - 1) / 
    meshlet.vidxDiv) + 1) & ~1;

  return meshlet;
}


inline vec2 oct_signNotZero(vec2 v) {
  return vec2((v.x >= 0) ? +1 : -1, (v.y >= 0) ? +1 : -1);
}

inline vec3 oct_to_vec3(vec2 e) {
  vec3 v = vec3(e.xy, 1 - abs(e.x) - abs(e.y));
  if(v.z < 0) v.xy = (1 - abs(v.yx)) * oct_signNotZero(v.xy);
  
  return normalize(v);
}
 
inline void decodeBbox(uvec4 meshletDesc, ObjectData object, 
  vec3& oBboxMin, vec3& oBboxMax) {
  vec3 bboxMin = unpackUnorm4x8(meshletDesc.x).xyz;
  vec3 bboxMax = unpackUnorm4x8(meshletDesc.y).xyz;
  
  vec3 objectExtent = (object.bboxMax.xyz - object.bboxMin.xyz);

  oBboxMin = bboxMin * objectExtent + object.bboxMin.xyz;
  oBboxMax = bboxMax * objectExtent + object.bboxMin.xyz;
}

inline void decodeNormalAngle(uvec4 meshletDesc, ObjectData object, 
  vec3& oNormal, float& oAngle) {
  uint packedVec =  meshletDesc.z;
  vec3 unpackedVec = unpackSnorm4x8(packedVec).xyz;
  
  oNormal = oct_to_vec3(unpackedVec.xy) * object.winding;
  oAngle = unpackedVec.z;
}

inline vec3 getBoxCorner(vec3 bboxMin, vec3 bboxMax, int n) {
  bvec3 useMax = bvec3((n & 1) != 0, (n & 2) != 0, (n & 4) != 0);
  return useMax ? bboxMax : bboxMin;
}

inline uint getCullBits(vec4 hPos) {
  uint cullBits = 0;
  cullBits |= hPos.x < -hPos.w ?  1 : 0;
  cullBits |= hPos.x >  hPos.w ?  2 : 0;
  cullBits |= hPos.y < -hPos.w ?  4 : 0;
  cullBits |= hPos.y >  hPos.w ?  8 : 0;
  cullBits |= hPos.z <  0      ? 16 : 0;
  cullBits |= hPos.z >  hPos.w ? 32 : 0;
  cullBits |= hPos.w <= 0      ? 64 : 0; 
  return cullBits;
}

inline bool earlyCull(uvec4 meshletDesc, ObjectData object) {
  vec3 bboxMin;
  vec3 bboxMax;
  decodeBbox(meshletDesc, object, bboxMin, bboxMax);

  vec3  oGroupNormal;
  float angle;
  decodeNormalAngle(meshletDesc, object, oGroupNormal, angle);

  vec3 wGroupNormal = normalize(mat3(object.worldMatrixIT) * oGroupNormal);
  bool backface = angle < 0;

  uint frustumBits = ~0;
  uint clippingBits = ~0;
  
  vec3 clipMin( 100000);
  vec3 clipMax(-100000);
  
  for(int n = 0; n < 8; n++) {
    vec4 wPos = object.worldMatrix * vec4(getBoxCorner(bboxMin, bboxMax, n), 1);
    vec4 hPos = scene.viewProjMatrix * wPos;
    frustumBits &= getCullBits(hPos);
    
    // approximate backface cone culling by testing against
    // bbox corners
    vec3 wDir = normalize(scene.viewPos.xyz - wPos.xyz);
    backface = backface && (dot(wGroupNormal, wDir) < angle);

    uint planeBits = 0;
    @meta for(int i = 0; i < NUM_CLIPPING_PLANES; i++)
      planeBits |= ((dot(scene.wClipPlanes[i], wPos) < 0) ? 1 : 0) << i;
    clippingBits &= planeBits;
  
    clipMin = min(clipMin, hPos.xyz / hPos.w);
    clipMax = max(clipMax, hPos.xyz / hPos.w);
  }
  
  return (frustumBits || backface || clippingBits);
}
