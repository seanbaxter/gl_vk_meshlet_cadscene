#include "shaders_common.hxx"

[[using spirv: buffer, binding(GEOMETRY_SSBO_PRIM), set(DSET_GEOMETRY)]]
uint primIndices1[];

[[using spirv: buffer, binding(GEOMETRY_SSBO_PRIM), set(DSET_GEOMETRY)]]
uvec2 primIndices2[];


[[using spirv: uniform, binding(GEOMETRY_TEX_IBO), set(DSET_GEOMETRY)]]
usamplerBuffer texIbo;

[[using spirv: uniform, binding(GEOMETRY_TEX_VBO), set(DSET_GEOMETRY)]]
samplerBuffer  texVbo;

[[using spirv: uniform, binding(GEOMETRY_TEX_ABO), set(DSET_GEOMETRY)]]
samplerBuffer  texAbo;

struct Task {
  uint baseID;
  uint8_t subIDs[GROUP_SIZE];
};

[[spirv::perTaskOut]] Task taskOut;
[[spirv::perTaskIn]] Task taskIn;

////////////////////////////////////////////////////////////////////////////////

[[using spirv: task, local_size(GROUP_SIZE)]]
void task_shader() {
  uint baseID = glcomp_WorkGroupID.x * GROUP_SIZE;
  uint laneID = glcomp_LocalInvocationID.x;

  baseID += push.assigns.x;
  uvec4 desc = meshletDescs[min(baseID + laneID, push.assigns.y) + 
    push.geometryOffsets.x];

  bool render = !(baseID + laneID > push.assigns.y || earlyCull(desc, object));
  
  uvec4 vote  = gl_subgroupBallot(render);
  uint  tasks = gl_subgroupBallotBitCount(vote);
  uint  voteGroup = vote.x;

  if (laneID == 0) {
    glmesh_TaskCount = tasks;
    taskOut.baseID = baseID;
  }

  uint idxOffset = gl_subgroupBallotExclusiveBitCount(vote);

  if (render)
    taskOut.subIDs[idxOffset] = laneID;
}

////////////////////////////////////////////////////////////////////////////////

template<int vert_count, bool clip_primitives>
vec4 procVertex(uint vert, uint vidx, uint meshletID) {
  // Stream the vertex position.
  vec3 oPos = texelFetch(texVbo, vidx).xyz;
  vec3 wPos = (object.worldMatrix  * vec4(oPos,1)).xyz;
  vec4 hPos = (scene.viewProjMatrix * vec4(wPos,1));
  
  glmesh_Output[vert].Position = hPos;
  shader_out<0, vec3[vert_count]>[vert] = wPos;
  shader_out<1, float[vert_count]>[vert] = 0;   // dummy

  // Stream the vertex normal.
  vec3 oNormal = texelFetch(texAbo, vidx).xyz;
  vec3 wNormal = mat3(object.worldMatrixIT) * oNormal;
  shader_out<2, vec3[vert_count]>[vert] = wNormal;

  // Stream the meshletID
  shader_out<3, int[vert_count]>[vert] = meshletID;
  
  // Perform clipping against user clip planes.
  if constexpr(clip_primitives) {
    glmesh_Output[vert].ClipDistance[0] = dot(scene.wClipPlanes[0], vec4(wPos,1));
    glmesh_Output[vert].ClipDistance[1] = dot(scene.wClipPlanes[1], vec4(wPos,1));
    glmesh_Output[vert].ClipDistance[2] = dot(scene.wClipPlanes[2], vec4(wPos,1));
  }

  return hPos;
}

template<int vert_count, int prim_count, bool clip_primitives>
[[using spirv:
  mesh(triangles, vert_count, prim_count), 
  local_size(GROUP_SIZE)
]]
void mesh_shader() {
  constexpr int vert_runs = div_up(vert_count, GROUP_SIZE);
  constexpr int index_runs = div_up(3 * prim_count, 8 * GROUP_SIZE);

  uvec4 geometryOffsets = shader_push<uvec4>;

  uint meshletID = taskIn.baseID + taskIn.subIDs[glcomp_WorkGroupID.x];
  uint laneID = glcomp_LocalInvocationID.x;

  // decode meshletDesc
  uvec4 desc = meshletDescs[meshletID + geometryOffsets.x];
  meshlet_t meshlet = decodeMeshlet(desc);

  meshlet.vidxStart += geometryOffsets.y / 4;
  meshlet.primStart += geometryOffsets.y / 4;

  uint primCount = meshlet.primMax + 1;
  uint vertCount = meshlet.vertMax + 1;
  
  // VERTEX PROCESSING
  for (int i = 0; i < vert_runs; ++i) {
    uint vert = laneID + i * GROUP_SIZE;
    uint vertLoad = min(vert, meshlet.vertMax);
    {
      uint idx   = vertLoad / meshlet.vidxDiv;
      uint shift = vertLoad & (meshlet.vidxDiv - 1);
      
      uint vidx = primIndices1[idx + meshlet.vidxStart];
      vidx <<= meshlet.vidxBits * (1 - shift); 
      vidx >>= meshlet.vidxBits;
      
      vidx += geometryOffsets.w;
    
      vec4 hPos = procVertex<vert_count, clip_primitives>(vert, vidx, meshletID);
    }
  }
  
  // PRIMITIVE TOPOLOGY  
  {
    uint readBegin = meshlet.primStart / 2;
    uint readIndex = primCount * 3 - 1;
    uint readMax   = readIndex / 8;

    for (int i = 0; i < index_runs; ++i) {
      uint read = laneID + i * GROUP_SIZE;
      uint readUsed = min(read, readMax);
      uvec2 topology = primIndices2[readBegin + readUsed];
      glmesh_writePackedPrimitiveIndices4x8(readUsed * 8 + 0, topology.x);
      glmesh_writePackedPrimitiveIndices4x8(readUsed * 8 + 4, topology.y);
    }
  }

  if (laneID == 0)
    glmesh_PrimitiveCount = primCount;
}

const mesh_shaders_t mesh_shaders {
  __spirv_data,
  __spirv_size,

  @spirv(task_shader),
  @spirv(mesh_shader<64, 126, false>),
  @spirv(mesh_shader<64, 126, true>),
};