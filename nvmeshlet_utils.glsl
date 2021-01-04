/* Copyright (c) 2016-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef USE_BACKFACECULL
#define USE_BACKFACECULL 1
#endif

#ifndef USE_SUBPIXELCULL
#define USE_SUBPIXELCULL 1
#endif

#ifndef USE_EARLY_BACKFACECULL
#define USE_EARLY_BACKFACECULL 1
#endif

#ifndef USE_EARLY_FRUSTUMCULL
#define USE_EARLY_FRUSTUMCULL 1
#endif

#ifndef USE_EARLY_SUBPIXELCULL
#define USE_EARLY_SUBPIXELCULL 1
#endif

#ifndef USE_EARLY_CLIPPINGCULL
#define USE_EARLY_CLIPPINGCULL 1
#endif

  /*
  Pack
    // x
    unsigned  bboxMinX : 8;
    unsigned  bboxMinY : 8;
    unsigned  bboxMinZ : 8;
    unsigned  vertexMax : 8;

    // y
    unsigned  bboxMaxX : 8;
    unsigned  bboxMaxY : 8;
    unsigned  bboxMaxZ : 8;
    unsigned  primMax : 8;
    
    // z
      signed  coneOctX : 8;
      signed  coneOctY : 8;
      signed  coneAngle : 8;
    unsigned  vertexBits : 8;
    
    // w
    unsigned  packOffset : 32;
    */

void decodeMeshlet( uvec4 meshletDesc, 
                    out uint vertMax, out uint primMax,
                    out uint primStart, out uint primDiv,
                    out uint vidxStart, out uint vidxBits, out uint vidxDiv)
{
  uint vMax  = (meshletDesc.x >> 24);
  uint packOffset = meshletDesc.w;
  
  vertMax    = vMax;
  primMax    = (meshletDesc.y >> 24);
  
  vidxStart  =  packOffset;
  vidxDiv    = (meshletDesc.z >> 24);
  vidxBits   = vidxDiv == 2 ? 16 : 0;
  
  primDiv    = 4;
  primStart  =  (packOffset + ((vMax + 1 + vidxDiv - 1) / vidxDiv) + 1) & ~1;
}


bool isMeshletValid(uvec4 meshletDesc)
{
  return meshletDesc.x != 0;
}

uint getMeshletNumTriangles(uvec4 meshletDesc)
{
  return (meshletDesc.y >> 24) + 1;
}

void decodeBbox(uvec4 meshletDesc, in ObjectData object, out vec3 oBboxMin, out vec3 oBboxMax)
{
  vec3 bboxMin = unpackUnorm4x8(meshletDesc.x).xyz;
  vec3 bboxMax = unpackUnorm4x8(meshletDesc.y).xyz;
  
  vec3 objectExtent = (object.bboxMax.xyz - object.bboxMin.xyz);

  oBboxMin = bboxMin * objectExtent + object.bboxMin.xyz;
  oBboxMax = bboxMax * objectExtent + object.bboxMin.xyz;
}

// oct_ code from "A Survey of Efficient Representations for Independent Unit Vectors"
// http://jcgt.org/published/0003/02/01/paper.pdf

vec2 oct_signNotZero(vec2 v) {
  return vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}

vec3 oct_to_vec3(vec2 e) {
  vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
  if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * oct_signNotZero(v.xy);
  
  return normalize(v);
}

void decodeNormalAngle(uvec4 meshletDesc, in ObjectData object, out vec3 oNormal, out float oAngle)
{
  uint packedVec =  meshletDesc.z;
  vec3 unpackedVec = unpackSnorm4x8(packedVec).xyz;
  
  oNormal = oct_to_vec3(unpackedVec.xy) * object.winding;
  oAngle = unpackedVec.z;
}

uint getCullBits(vec4 hPos)
{
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

void pixelBboxEpsilon(inout vec2 pixelMin, inout vec2 pixelMax)
{
  // Apply some safety around the bbox to take into account fixed point rasterization.
  // This logic will only work without MSAA active.
  
  const float epsilon = (1.0 / 256.0);
  pixelMin -= epsilon;
  pixelMax += epsilon;
}

bool pixelBboxCull(vec2 pixelMin, vec2 pixelMax){
  // bbox culling
  pixelMin = round(pixelMin);
  pixelMax = round(pixelMax);
  return ( ( pixelMin.x == pixelMax.x) || ( pixelMin.y == pixelMax.y));
}

//////////////////////////////////////////////////////////////////


vec4 getBoxCorner(vec3 bboxMin, vec3 bboxMax, int n)
{
  bvec3 useMax = bvec3((n & 1) != 0, (n & 2) != 0, (n & 4) != 0);
  return vec4(mix(bboxMin, bboxMax, useMax),1);

}

bool earlyCull(uvec4 meshletDesc, in ObjectData object)
{
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
  
  vec3 clipMin = vec3( 100000);
  vec3 clipMax = vec3(-100000);
  
  for (int n = 0; n < 8; n++){
    vec4 wPos = object.worldMatrix * getBoxCorner(bboxMin, bboxMax, n);
    vec4 hPos = scene.viewProjMatrix * wPos;
    frustumBits &= getCullBits(hPos);
    // approximate backface cone culling by testing against
    // bbox corners
    vec3 wDir = normalize(scene.viewPos.xyz - wPos.xyz);
    backface = backface && (dot(wGroupNormal, wDir) < angle);
 
    uint planeBits = 0;
    for (int i = 0; i < NUM_CLIPPING_PLANES; i++){
      planeBits |= ((dot(scene.wClipPlanes[i], wPos) < 0) ? 1 : 0) << i;
    }
    clippingBits &= planeBits;
  
    clipMin = min(clipMin, hPos.xyz / hPos.w);
    clipMax = max(clipMax, hPos.xyz / hPos.w);
  }
  
  
  return frustumBits != 0 || backface || clippingBits != 0;
}


//////////////////////////////////////////////////////////////////


vec2 getScreenPos(vec4 hPos)
{
  hPos /= hPos.w;
  return vec2((hPos.xy * 0.5 + 0.5) * scene.viewportf);
}

int testTriangle(vec2 a, vec2 b, vec2 c, float winding, bool frustum)
{

  // back face culling
  vec2 ab = b.xy - a.xy;
  vec2 ac = c.xy - a.xy;
  float cross_product = ab.x * ac.y - ab.y * ac.x;   

  // Vulkan's upper-left window origin means that screen coordinates 
  // are reversed relative to OpenGL's.  Reverse the sign of the
  // cross-product to compensate.
  cross_product = -cross_product;


  // compute the min and max in each X and Y direction
  vec2 pixelMin = min(a,min(b,c));
  vec2 pixelMax = max(a,max(b,c));
  
  pixelBboxEpsilon(pixelMin, pixelMax);

  // viewport culling
  if (frustum && ((pixelMax.x < 0) || (pixelMin.x >= scene.viewportf.x) || (pixelMax.y < 0) || (pixelMin.y >= scene.viewportf.y))) return 0;

  return 1;
}

int testTriangle(vec2 a, vec2 b, vec2 c, float winding, uint abits, uint bbits, uint cbits)
{
  if ((abits & bbits & cbits) == 0){
    return testTriangle(a,b,c,winding,false);
  }
  return 0;
}
