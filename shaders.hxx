#pragma once

struct raster_shaders_t {
  const char* spirv_data;
  size_t spirv_size;

  const char* vert[2];   // Without and with clipping.
  const char* frag;
};
extern const raster_shaders_t raster_shaders;

struct bbox_shaders_t {
  const char* spirv_data;
  size_t spirv_size;

  const char* vert;
  const char* geom;
  const char* frag;
};
extern const bbox_shaders_t bbox_shaders;
extern const bbox_shaders_t bbox_shaders2;

struct mesh_shaders_t {
  const char* spirv_data;
  size_t spirv_size;

  const char* task;
  const char* mesh[2];  // Without and with clipping.
};
extern const mesh_shaders_t mesh_shaders;