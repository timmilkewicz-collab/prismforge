include(FetchContent)

FetchContent_Declare(spout2
  GIT_REPOSITORY https://github.com/leadedge/Spout2.git
  GIT_TAG f49e2f469f8cb25f559a6eaa61a3f5b8173fc100
  GIT_SHALLOW FALSE)
set(SPOUT_BUILD_SPOUTDX ON CACHE BOOL "" FORCE)
set(SPOUT_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(SPOUT_BUILD_CMT OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spout2)

if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
  # The root vcpkg.json pins these ports. miniaudio is a header-only port.
  find_package(nlohmann_json CONFIG REQUIRED)
  find_path(MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)
else()
  # Local builds may use the same pinned upstream sources without vcpkg.
  FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
    GIT_SHALLOW FALSE)
  FetchContent_GetProperties(miniaudio)
  if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
  endif()
  set(MINIAUDIO_INCLUDE_DIR "${miniaudio_SOURCE_DIR}")

  set(JSON_BuildTests OFF CACHE INTERNAL "")
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

add_executable(PrismForge.Engine
  main.cpp
  AudioCapture.cpp
  MidiBridge.cpp
  OscBridge.cpp
  PipeServer.cpp
  ShowStore.cpp
  SpoutRender.cpp)
target_compile_definitions(PrismForge.Engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_features(PrismForge.Engine PRIVATE cxx_std_20)
target_include_directories(PrismForge.Engine PRIVATE ${MINIAUDIO_INCLUDE_DIR})
target_link_libraries(PrismForge.Engine PRIVATE
  prismforge_core
  SpoutDX_static
  nlohmann_json::nlohmann_json
  d3d11 dxgi d3dcompiler shell32 ole32 ws2_32 winmm)
if(MSVC)
  target_compile_options(PrismForge.Engine PRIVATE /W4 /permissive-)
endif()
set_target_properties(PrismForge.Engine PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_command(TARGET PrismForge.Engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${PROJECT_SOURCE_DIR}/assets/shaders"
    "$<TARGET_FILE_DIR:PrismForge.Engine>/assets/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${PROJECT_SOURCE_DIR}/assets/scenes"
    "$<TARGET_FILE_DIR:PrismForge.Engine>/assets/scenes")
