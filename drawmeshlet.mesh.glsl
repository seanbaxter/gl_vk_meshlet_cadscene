/* Copyright (c) 2016-2018, NVIDIA CORPORATION. All rights reserved.
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

// ide.config.nvglslcchip="tu100"

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_shading_language_include : enable

#include "config.h"

//////////////////////////////////////

  #extension GL_NV_mesh_shader : require

//////////////////////////////////////

  // one of them provides uint8_t
  #extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
  #extension GL_NV_gpu_shader5 : enable
    
  #extension GL_KHR_shader_subgroup_basic : require
  #extension GL_KHR_shader_subgroup_ballot : require
  #extension GL_KHR_shader_subgroup_vote : require

//////////////////////////////////////

#include "common.h"

//////////////////////////////////////////////////
// MESH CONFIG

#define GROUP_SIZE    WARP_SIZE

layout(local_size_x=GROUP_SIZE) in;
layout(max_vertices=NVMESHLET_VERTEX_COUNT, max_primitives=NVMESHLET_PRIMITIVE_COUNT) out;
layout(triangles) out;

#define USE_MESH_SHADERCULL     0
#define USE_EARLY_ATTRIBUTES    1
#define USE_TASK_STAGE          1
#define USE_MESH_FRUSTUMCULL    0


// UNIFORMS

layout(push_constant) uniform pushConstant{
  uvec4     geometryOffsets;
  // x: mesh, y: prim, z: index, w: vertex
};

layout(std140, binding = SCENE_UBO_VIEW, set = DSET_SCENE) uniform sceneBuffer {
  SceneData scene;
};
layout(std140, binding= 0, set = DSET_OBJECT) uniform objectBuffer {
  ObjectData object;
};

layout(std430, binding = GEOMETRY_SSBO_MESHLETDESC, set = DSET_GEOMETRY) buffer meshletDescBuffer {
  uvec4 meshletDescs[];
};
layout(std430, binding = GEOMETRY_SSBO_PRIM, set = DSET_GEOMETRY) buffer primIndexBuffer1 {
  uint  primIndices1[];
};
layout(std430, binding = GEOMETRY_SSBO_PRIM, set = DSET_GEOMETRY) buffer primIndexBuffer2 {
  uvec2 primIndices2[];
};

layout(binding=GEOMETRY_TEX_IBO,  set=DSET_GEOMETRY)  uniform usamplerBuffer texIbo;
layout(binding=GEOMETRY_TEX_VBO,  set=DSET_GEOMETRY)  uniform samplerBuffer  texVbo;
layout(binding=GEOMETRY_TEX_ABO,  set=DSET_GEOMETRY)  uniform samplerBuffer  texAbo;

/////////////////////////////////////////////////

#include "nvmeshlet_utils.glsl"

/////////////////////////////////////////////////
// MESH INPUT

  taskNV in Task {
    uint    baseID;
    uint8_t subIDs[GROUP_SIZE];
  } IN;
  // gl_WorkGroupID.x runs from [0 .. parentTask.gl_TaskCountNV - 1]
  uint meshletID = IN.baseID + IN.subIDs[gl_WorkGroupID.x];
  uint laneID = gl_LocalInvocationID.x;


////////////////////////////////////////////////////////////
// INPUT

// If you work from fixed vertex definitions and don't need dynamic 
// format conversions by texture formats, or don't mind
// creating multiple shader permutations, you may want to
// use ssbos here, instead of tbos for a bit more performance.

vec3 getPosition( uint vidx ){
  return texelFetch(texVbo, int(vidx)).xyz;
}

vec3 getNormal( uint vidx ){
  return texelFetch(texAbo, int(vidx * 1)).xyz;
}


////////////////////////////////////////////////////////////
// OUTPUT

layout(location=0) out Interpolants {
  vec3  wPos;
  float dummy;
  vec3  wNormal;
  flat uint meshletID;
} OUT[];

//////////////////////////////////////////////////
// EXECUTION

vec4 procVertex(const uint vert, uint vidx)
{
  vec3 oPos = getPosition(vidx);
  vec3 wPos = (object.worldMatrix  * vec4(oPos,1)).xyz;
  vec4 hPos = (scene.viewProjMatrix * vec4(wPos,1));
  
  gl_MeshVerticesNV[vert].gl_Position = hPos;
  
  OUT[vert].wPos = wPos;
  OUT[vert].dummy = 0;
  OUT[vert].meshletID = meshletID;
  
  vec3 oNormal = getNormal(vidx);
  vec3 wNormal = mat3(object.worldMatrixIT) * oNormal;
  OUT[vert].wNormal = wNormal;

  // spir-v annoyance, doesn't unroll the loop and therefore cannot derive the number of clip distances used
  // gl_MeshVerticesNV[vert].gl_ClipDistance[0] = dot(scene.wClipPlanes[0], vec4(wPos,1));
  // gl_MeshVerticesNV[vert].gl_ClipDistance[1] = dot(scene.wClipPlanes[1], vec4(wPos,1));
  // gl_MeshVerticesNV[vert].gl_ClipDistance[2] = dot(scene.wClipPlanes[2], vec4(wPos,1));
  
  return hPos;
}

  // if you never intend to use the above mechanism,
  // you can express the attribute processing more like a regular
  // vertex shader

  #define NVMSH_INDEX_BITS      8
  #define NVMSH_PACKED4X8_GET(packed, idx)   (((packed) >> (NVMSH_INDEX_BITS * (idx))) & 255)
  
  
  // only for tight packing case, 8 indices are loaded per thread
  #define NVMSH_PRIMITIVE_INDICES_RUNS  ((NVMESHLET_PRIMITIVE_COUNT * 3 + GROUP_SIZE * 8 - 1) / (GROUP_SIZE * 8))

  // processing loops
  #define NVMSH_VERTEX_RUNS     ((NVMESHLET_VERTEX_COUNT + GROUP_SIZE - 1) / GROUP_SIZE)
  #define NVMSH_PRIMITIVE_RUNS  ((NVMESHLET_PRIMITIVE_COUNT + GROUP_SIZE - 1) / GROUP_SIZE)
  
  #define nvmsh_writePackedPrimitiveIndices4x8NV writePackedPrimitiveIndices4x8NV

///////////////////////////////////////////////////////////////////////////////

void main()
{
  // decode meshletDesc
  uvec4 desc = meshletDescs[meshletID + geometryOffsets.x];
  uint vertMax;
  uint primMax;
    uint vidxStart;
    uint vidxBits;
    uint vidxDiv;
    uint primStart;
    uint primDiv;
    decodeMeshlet(desc, vertMax, primMax, primStart, primDiv, vidxStart, vidxBits, vidxDiv);

    vidxStart += geometryOffsets.y / 4;
    primStart += geometryOffsets.y / 4;



  uint primCount = primMax + 1;
  uint vertCount = vertMax + 1;
  
  
  for (uint i = 0; i < uint(NVMSH_VERTEX_RUNS); i++) 
  {
    uint vert = laneID + i * GROUP_SIZE;
    uint vertLoad = min(vert, vertMax);
    {
      uint idx   = (vertLoad) / vidxDiv;
      uint shift = (vertLoad) & (vidxDiv-1);
      
      uint vidx = primIndices1[idx + vidxStart];
      vidx <<= vidxBits * (1-shift); 
      vidx >>= vidxBits;
      
      vidx += geometryOffsets.w;
    
      vec4 hPos = procVertex(vert, vidx);
    }
  }

  
  // PRIMITIVE TOPOLOGY  
  {
    uint readBegin = primStart / 2;
    uint readIndex = primCount * 3 - 1;
    uint readMax   = readIndex / 8;

    for (uint i = 0; i < uint(NVMSH_PRIMITIVE_INDICES_RUNS); i++)
    {
      uint read = laneID + i * GROUP_SIZE;
      uint readUsed = min(read, readMax);
      uvec2 topology = primIndices2[readBegin + readUsed];
      nvmsh_writePackedPrimitiveIndices4x8NV(readUsed * 8 + 0, topology.x);
      nvmsh_writePackedPrimitiveIndices4x8NV(readUsed * 8 + 4, topology.y);
    }
  }

  if (laneID == 0) {
    gl_PrimitiveCountNV = primCount;
  }
}