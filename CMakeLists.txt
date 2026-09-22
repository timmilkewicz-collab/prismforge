cmake_minimum_required(VERSION 3.25)
project(PrismForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "PrismForge's native runtime currently targets Windows only")
endif()

include(CTest)

add_subdirectory(native/core)
add_subdirectory(native/engine)
add_subdirectory(native/launcher)

if(BUILD_TESTING)
  add_subdirectory(native/tests)
endif()
