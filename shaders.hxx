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
extern raster_shaders_t raster_shaders;