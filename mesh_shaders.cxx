#include "shaders_common.hxx"

#if 0

[[spirv::perTask]] struct {
  uint baseID;
  uint8_t subIDs[GROUP_SIZE];
} task;

[[using spirv:
  mesh(triangles, NVMESHLET_VERTEX_COUNT, NVMESHLET_PRIMITIVE_COUNT), 
  local_size(GROUP_SIZE)
]]
void mesh_shader() {
  uint meshletID = task.baseID + task.subIDs[glcomp_WorkGroupID.x];
  uint laneID = glcomp_LocalInvocationID.x;

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
  

  // LOAD PHASE
  // VERTEX PROCESSING
  
  for (uint i = 0; i < uint(NVMSH_VERTEX_RUNS); i++) 
  {
    uint vert = laneID + i * GROUP_SIZE;
    uint vertLoad = min(vert, vertMax);
    clearVertexUsed(vert);
    {
      uint idx   = (vertLoad) / vidxDiv;
      uint shift = (vertLoad) & (vidxDiv-1);
      
      uint vidx = primIndices1[idx + vidxStart];
      vidx <<= vidxBits * (1-shift); 
      vidx >>= vidxBits;
      
      vidx += geometryOffsets.w;
    
      vec4 hPos = procVertex(vert, vidx);
      setVertexClip(vert, getCullBits(hPos));
      
    #if USE_EARLY_ATTRIBUTES
      procAttributes(vert, vidx);
    #else
      writeVertexIndex(vert, vidx);
    #endif
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

  uint outTriangles = 0;
  
  NVMSH_BARRIER();
  
  // PRIMITIVE PHASE
 
  const uint primRuns = (primCount + GROUP_SIZE - 1) / GROUP_SIZE;
  for (uint i = 0; i < primRuns; i++) {
    uint triCount = 0;
    uint topology = 0;
    
    uint prim = laneID + i * GROUP_SIZE;
    
    if (prim <= primMax) {
      uint idx = prim * 3;
      uint ia = gl_PrimitiveIndicesNV[idx + 0];
      uint ib = gl_PrimitiveIndicesNV[idx + 1];
      uint ic = gl_PrimitiveIndicesNV[idx + 2];
      topology = ia | (ib << NVMSH_INDEX_BITS) | (ic << (NVMSH_INDEX_BITS*2));
      
      // build triangle
      vec2 a = getVertexScreen(ia);
      vec2 b = getVertexScreen(ib);
      vec2 c = getVertexScreen(ic);

      uint abits = getVertexClip(ia);
      uint bbits = getVertexClip(ib);
      uint cbits = getVertexClip(ic);
      
      triCount = testTriangle(a.xy, b.xy, c.xy, 1.0, abits, bbits, cbits);

      
      if (triCount != 0) {
        setVertexUsed(ia);
        setVertexUsed(ib);
        setVertexUsed(ic);
      }
    }
    
    uvec4 vote = subgroupBallot(triCount == 1);
    uint  tris = subgroupBallotBitCount(vote);
    uint  idxOffset = outTriangles + subgroupBallotExclusiveBitCount(vote);

  
    if (triCount != 0) {
      uint idx = idxOffset * 3;
      gl_PrimitiveIndicesNV[idx + 0] = NVMSH_PACKED4X8_GET(topology, 0);
      gl_PrimitiveIndicesNV[idx + 1] = NVMSH_PACKED4X8_GET(topology, 1);
      gl_PrimitiveIndicesNV[idx + 2] = NVMSH_PACKED4X8_GET(topology, 2);
    }
    
    outTriangles += tris;
  }

  
  NVMSH_BARRIER();
  
  if (laneID == 0)
    gl_PrimitiveCountNV = outTriangles;
}
#endif
const mesh_shaders_t mesh_shaders {
  __spirv_data,
  __spirv_size,

};