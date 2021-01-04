#pragma once

struct raster_shaders_t {
  const char* spirv_data;
  size_t spirv_size;

  struct pairs_t {
    const char* vert;
    const char* frag;
  };

  // Support up to 4 extra interpolants.
  pairs_t pairs[5];
};
extern const raster_shaders_t raster_shaders;

struct bbox_shaders_t {
  const char* spirv_data;
  size_t spirv_size;

  const char* vert;
  const char* geom[2][2];
  const char* frag;
};
extern const bbox_shaders_t bbox_shaders;

struct mesh_shaders_t {
  const char* spirv_data;
  size_t spirv_size;

  const char* task;
  const char* mesh;
};
extern const mesh_shaders_t mesh_shaders;