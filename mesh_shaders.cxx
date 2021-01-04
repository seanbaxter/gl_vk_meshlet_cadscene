#if 0

#include "shaders.hxx"
#include "config.h"

[[using spirv:
  mesh(triangles, NVMESHLET_VERTEX_COUNT, NVMESHLET_PRIMITIVE_COUNT), 
  local_size(WARP_SIZE)
]]
void mesh_shader() {
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

#if !USE_MESH_SHADERCULL
  if (laneID == 0) {
    gl_PrimitiveCountNV = primCount;
  #if USE_STATS
    atomicAdd(stats.meshletsOutput, 1);
    atomicAdd(stats.trisOutput, primCount);
    atomicAdd(stats.attrInput,  vertCount);
    atomicAdd(stats.attrOutput, vertCount);
  #endif
  }
#else
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

    #if USE_MESH_FRUSTUMCULL
      uint abits = getVertexClip(ia);
      uint bbits = getVertexClip(ib);
      uint cbits = getVertexClip(ic);
      
      triCount = testTriangle(a.xy, b.xy, c.xy, 1.0, abits, bbits, cbits);
    #else
      triCount = testTriangle(a.xy, b.xy, c.xy, 1.0, false);
    #endif
      
      if (triCount != 0) {
        setVertexUsed(ia);
        setVertexUsed(ib);
        setVertexUsed(ic);
      }
    }
    
  #if IS_VULKAN
    uvec4 vote = subgroupBallot(triCount == 1);
    uint  tris = subgroupBallotBitCount(vote);
    uint  idxOffset = outTriangles + subgroupBallotExclusiveBitCount(vote);
  #else
    uint vote = ballotThreadNV(triCount == 1);
    uint tris = bitCount(vote);
    uint idxOffset = outTriangles + bitCount(vote & gl_ThreadLtMaskNV);
  #endif
  
    if (triCount != 0) {
      uint idx = idxOffset * 3;
      gl_PrimitiveIndicesNV[idx + 0] = NVMSH_PACKED4X8_GET(topology, 0);
      gl_PrimitiveIndicesNV[idx + 1] = NVMSH_PACKED4X8_GET(topology, 1);
      gl_PrimitiveIndicesNV[idx + 2] = NVMSH_PACKED4X8_GET(topology, 2);
    }
    
    outTriangles += tris;
  }

  
  NVMSH_BARRIER();
  
  if (laneID == 0) {
    gl_PrimitiveCountNV = outTriangles;
  #if USE_STATS
    atomicAdd(stats.meshletsOutput, 1);
    atomicAdd(stats.trisOutput, outTriangles);
    #if USE_EARLY_ATTRIBUTES
      atomicAdd(stats.attrInput,  vertCount);
      atomicAdd(stats.attrOutput, vertCount);
    #endif
  #endif
  }

#if !USE_EARLY_ATTRIBUTES
  // FETCH REST OF VERTEX ATTRIBS
  
  #if USE_BATCHED_LATE_FETCH
  {
    // use two dedicated phases, which reduces the 
    // overall amount of latency, if compiler 
    // is smart enough to keep temp in registers
    // and not use local memory.
    //
    // - load  run R
    // - load  run R+1
    // ...
    // ! wait for loads
    // - write run R
    // - write run R+1
    // ...
    
    TempAttributes tempattrs[NVMSH_VERTEX_RUNS];
    
    for (uint i = 0; i < uint(NVMSH_VERTEX_RUNS); i++) {
      uint vert = laneID + i * GROUP_SIZE;
      bool used = isVertexUsed( vert );
      if (used) {
        uint vidx = readVertexIndex( vert );
        fetchAttributes(tempattrs[i], vert, vidx);
      }
    }
    for (uint i = 0; i < uint(NVMSH_VERTEX_RUNS); i++) {
      uint vert = laneID + i * GROUP_SIZE;
      bool used = isVertexUsed( vert );
      if (used) {
        uint vidx = readVertexIndex( vert );
        storeAttributes(tempattrs[i],vert, vidx);
      }
    }
  }
  #else
  {
    // due to dynamic branching the compiler may not unroll 
    // all loads prior all writes, which adds latency 
    // - load  run R
    // ! wait for loads
    // - write run R
    // - load  run R+1
    // ! wait for loads
    // - write run R+1
    // ...
    //
    // FIXME get compiler to do above with simple code
  
    for (uint i = 0; i < uint(NVMSH_VERTEX_RUNS); i++) {
      uint vert = laneID + i * GROUP_SIZE;
      bool used = isVertexUsed( vert );
      if (used) {
        uint vidx = readVertexIndex( vert );
        procAttributes(vert, vidx);
      }
    }
  }
  #endif
  
  #if USE_STATS
  {
    uint usedVertices = 0;
    for (uint i = 0; i < uint(NVMSH_VERTEX_RUNS); i++) {
      uint vert = laneID + i * GROUP_SIZE;
      bool used = isVertexUsed( vert );
    #if IS_VULKAN
      uvec4 vote  = subgroupBallot(used);
      uint  verts = subgroupBallotBitCount(vote);
    #else
      uint vote  = ballotThreadNV(used);
      uint verts = bitCount(vote);
    #endif
      usedVertices += verts;
    }
    if (laneID == 0){
      atomicAdd(stats.attrInput, vertCount);
      atomicAdd(stats.attrOutput, usedVertices);
    }
  }
  #endif
  
#endif // !USE_EARLY_ATTRIBUTES
#endif // !USE_MESH_SHADERCULL
}
#endif