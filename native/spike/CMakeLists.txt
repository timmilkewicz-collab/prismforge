cmake_minimum_required(VERSION 3.24)
project(PrismForgeSpoutSpike LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The Spout feasibility spike requires Windows")
endif()

include(FetchContent)

# This tag is pinned so the API and shader-sharing behavior cannot change
# between builds. To use a local checkout, pass
# -DFETCHCONTENT_SOURCE_DIR_SPOUT2=<absolute-path-to-Spout2>.
FetchContent_Declare(spout2
  GIT_REPOSITORY https://github.com/leadedge/Spout2.git
  GIT_TAG f49e2f469f8cb25f559a6eaa61a3f5b8173fc100
  GIT_SHALLOW FALSE
)

set(SPOUT_BUILD_SPOUTDX ON CACHE BOOL "" FORCE)
set(SPOUT_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(SPOUT_BUILD_CMT OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spout2)

add_executable(PrismForge.SpoutSpike main.cpp)
target_compile_features(PrismForge.SpoutSpike PRIVATE cxx_std_20)
target_compile_definitions(PrismForge.SpoutSpike PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(PrismForge.SpoutSpike PRIVATE
  SpoutDX_static
  d3d11
  dxgi
  d3dcompiler
)
set_target_properties(PrismForge.SpoutSpike PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Deterministic effect check: render at a fixed time, receive each Spout frame,
# and compare the GPU output against the no-effect frame.
add_executable(PrismForge.EffectProbe
  effect_probe.cpp
  ../engine/SpoutRender.cpp
)
target_compile_features(PrismForge.EffectProbe PRIVATE cxx_std_20)
target_compile_definitions(PrismForge.EffectProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(PrismForge.EffectProbe PRIVATE
  ../engine
  ../core/include
)
target_link_libraries(PrismForge.EffectProbe PRIVATE
  SpoutDX_static
  d3d11
  dxgi
  d3dcompiler
)
set_target_properties(PrismForge.EffectProbe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
