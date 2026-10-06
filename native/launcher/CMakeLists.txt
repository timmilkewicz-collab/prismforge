if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.25)
  project(PrismForgeLauncher LANGUAGES CXX)
  include(CTest)
endif()

add_executable(PrismForge.Launcher WIN32 main.cpp LauncherOptions.cpp)
target_compile_features(PrismForge.Launcher PRIVATE cxx_std_20)
target_compile_definitions(PrismForge.Launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(PrismForge.Launcher PRIVATE shell32 user32)
if(MSVC)
  target_compile_options(PrismForge.Launcher PRIVATE /W4 /permissive-)
endif()
set_target_properties(PrismForge.Launcher PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(BUILD_TESTING)
  add_test(NAME launcher_explicit_layout
    COMMAND $<TARGET_FILE:PrismForge.Launcher> --check-layout
      --engine $<TARGET_FILE:PrismForge.Launcher>
      --control $<TARGET_FILE:PrismForge.Launcher>)
  add_test(NAME launcher_layout_with_launchpad_flag
    COMMAND $<TARGET_FILE:PrismForge.Launcher> --check-layout --launchpad
      --engine $<TARGET_FILE:PrismForge.Launcher>
      --control $<TARGET_FILE:PrismForge.Launcher>)
  add_test(NAME launcher_layout_with_legacy_recursive_audio
    COMMAND $<TARGET_FILE:PrismForge.Launcher> --check-layout
      --legacy-recursive-audio
      --engine $<TARGET_FILE:PrismForge.Launcher>
      --control $<TARGET_FILE:PrismForge.Launcher>)

  add_executable(prismforge_launcher_options_tests
    LauncherOptionsTests.cpp LauncherOptions.cpp)
  target_compile_features(prismforge_launcher_options_tests PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(prismforge_launcher_options_tests PRIVATE /W4 /permissive-)
  endif()
  add_test(NAME prismforge_launcher_options_tests
    COMMAND prismforge_launcher_options_tests)
endif()
