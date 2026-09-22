if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
  find_package(nlohmann_json CONFIG REQUIRED)
endif()

add_executable(prismforge_core_tests CoreTests.cpp)
target_link_libraries(prismforge_core_tests PRIVATE prismforge_core)
add_test(NAME prismforge_core_tests COMMAND prismforge_core_tests)

add_executable(prismforge_osc_tests
  OscTests.cpp
  ../engine/OscBridge.cpp)
target_include_directories(prismforge_osc_tests PRIVATE ../engine)
target_compile_definitions(prismforge_osc_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(prismforge_osc_tests PRIVATE
  prismforge_core nlohmann_json::nlohmann_json ws2_32)
if(MSVC)
  target_compile_options(prismforge_osc_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME prismforge_osc_tests COMMAND prismforge_osc_tests)

add_executable(prismforge_midi_tests
  MidiTests.cpp
  ../engine/MidiBridge.cpp)
target_include_directories(prismforge_midi_tests PRIVATE ../engine)
target_compile_definitions(prismforge_midi_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(prismforge_midi_tests PRIVATE prismforge_core winmm)
if(MSVC)
  target_compile_options(prismforge_midi_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME prismforge_midi_tests COMMAND prismforge_midi_tests)
