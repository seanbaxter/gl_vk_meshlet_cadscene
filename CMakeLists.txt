cmake_minimum_required(VERSION 2.8)

get_filename_component(PROJNAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
Project(${PROJNAME})
Message(STATUS "-------------------------------")
Message(STATUS "Processing Project ${PROJNAME}:")

SET(MODEL_DOWNLOAD_${PROJNAME} ON CACHE BOOL "CHECK to download the 3d models needed for this sample" )

#####################################################################################
# look for shared_sources 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find CMakeLists_include.txt yet
#
if(NOT BASE_DIRECTORY) # if not defined, it means this cmake file was called as the first entry point and not included
  # check if the external repository is outside or inside the project (as a sub-module)
  # testing the file CMakeLists_include.txt because when sub-modules are not cloned, the folders are still there...
  # we also assume here that if shared_sources is there, shared_external is, too...
  SET(BASE_DIRECTORY "" CACHE FILEPATH "folder containing shared_sources")
  SET(ADD_SUBDIR_BELOW 1)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shared_sources/CMakeLists_include.txt)
    SET(BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../shared_sources/CMakeLists_include.txt)
    SET(BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
  elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../shared_sources/CMakeLists_include.txt)
    SET(BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  else()
  endif()
endif(NOT BASE_DIRECTORY)

if(EXISTS ${BASE_DIRECTORY}/shared_sources/CMakeLists_include.txt)
  INCLUDE(${BASE_DIRECTORY}/shared_sources/CMakeLists_include.txt)
else()
  Message(FATAL_ERROR "could not find base directory, please set BASE_DIRECTORY to folder containing shared_sources")
endif()
    
_add_project_definitions(${PROJNAME})

set( BUILD_${PROJNAME}_VULKAN_ONLY FALSE CACHE BOOL "Avoids OpenGL in samples that support dual use" )

#####################################################################################
# additions from packages needed for this sample
# add refs  in LIBRARIES_OPTIMIZED
# add refs  in LIBRARIES_DEBUG
# add files in PACKAGE_SOURCE_FILES

set( VK_EXENAME "vk_meshlet_cadscene" )

_add_package_VulkanSDK()
_add_package_ShaderC()
_add_package_IMGUI()
_add_package_ZLIB()

#####################################################################################
_find_package_OpenMP()
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()


#####################################################################################
# process the rest of some cmake code that needs to be done *after* the packages add
_add_shared_sources_lib()

add_definitions(-DCSF_ZIP_SUPPORT=1)

#####################################################################################
# download models
unset(ASSET_FILES)
if(MODEL_DOWNLOAD_${PROJNAME})
  set(FILELIST 
  "worldcar.csf.gz"
  "blade.csf.gz"
  )
  _download_files(
    "${FILELIST}"
    "http://developer.download.nvidia.com/ProGraphics/nvpro-samples"
    ${DOWNLOAD_TARGET_DIR}
    0
    ASSET_FILES
  )
endif()

#####################################################################################
# Source files for this project
#
file(GLOB VK_SOURCE_FILES *.cpp *.hpp *.inl *.h *.c)
file(GLOB GLSL_FILES *.glsl)

list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/gl_nv_mesh_shader.h)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/gl_nv_mesh_shader.cpp)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/gl_nv_draw_vulkan_image.h)
list(REMOVE_ITEM VK_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/gl_nv_draw_vulkan_image.cpp)

#####################################################################################
# common source code needed for this sample
#
source_group(common FILES 
  ${COMMON_SOURCE_FILES}
  ${PACKAGE_SOURCE_FILES}
)
source_group(shaders FILES 
  ${GLSL_FILES}
)

set(UNIXLINKLIBS dl pthread openmp)

add_executable(${VK_EXENAME} ${VK_SOURCE_FILES} ${COMMON_SOURCE_FILES} ${PACKAGE_SOURCE_FILES} ${GLSL_FILES})

#####################################################################################
# Linkage
#

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${VK_EXENAME} PRIVATE Threads::Threads)
target_link_libraries(${VK_EXENAME} ${PLATFORM_LIBRARIES} shared_sources)

foreach(DEBUGLIB ${LIBRARIES_DEBUG})
  target_link_libraries(${VK_EXENAME} debug ${DEBUGLIB})
endforeach(DEBUGLIB)

foreach(RELEASELIB ${LIBRARIES_OPTIMIZED})
  target_link_libraries(${VK_EXENAME} optimized ${RELEASELIB})
endforeach(RELEASELIB)


#####################################################################################
# copies binaries that need to be put next to the exe files (ZLib, etc.)
#

_copy_binaries_to_target( ${VK_EXENAME} )
LIST(APPEND GLSL_FILES "common.h")
LIST(APPEND GLSL_FILES "config.h")
LIST(APPEND ASSET_FILES "blade_meshlet.cfg")
LIST(APPEND ASSET_FILES "blade_meshlet_viewpoints.txt")
LIST(APPEND ASSET_FILES "worldcar_meshlet.cfg")
LIST(APPEND ASSET_FILES "worldcar_meshlet_viewpoints.txt")

install(FILES ${GLSL_FILES} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/GLSL_${PROJNAME}")
install(FILES ${GLSL_FILES} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/GLSL_${PROJNAME}")
install(FILES ${ASSET_FILES} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug")
install(FILES ${ASSET_FILES} CONFIGURATIONS Release DESTINATION "bin_${ARCH}")
