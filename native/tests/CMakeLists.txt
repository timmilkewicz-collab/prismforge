if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
  find_package(nlohmann_json CONFIG REQUIRED)
endif()

add_executable(prismforge_core_tests CoreTests.cpp)
target_link_libraries(prismforge_core_tests PRIVATE prismforge_core)
add_test(NAME prismforge_core_tests COMMAND prismforge_core_tests)

add_executable(prismforge_musical_state_tests MusicalStateTests.cpp)
target_link_libraries(prismforge_musical_state_tests PRIVATE prismforge_core)
target_compile_definitions(prismforge_musical_state_tests PRIVATE
  PRISMFORGE_MUSICAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/musical")
if(MSVC)
  target_compile_options(prismforge_musical_state_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME prismforge_musical_state_tests COMMAND prismforge_musical_state_tests)

add_executable(prismforge_telemetry_tests
  TelemetryTests.cpp
  ../engine/SignalTelemetry.cpp)
target_include_directories(prismforge_telemetry_tests PRIVATE ../engine)
target_link_libraries(prismforge_telemetry_tests PRIVATE
  prismforge_core nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(prismforge_telemetry_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME prismforge_telemetry_tests COMMAND prismforge_telemetry_tests)

find_program(PRISMFORGE_POWERSHELL_EXECUTABLE NAMES powershell REQUIRED)
add_test(NAME prismforge_musical_monitor_parser
  COMMAND "${PRISMFORGE_POWERSHELL_EXECUTABLE}" -NoProfile
    -ExecutionPolicy Bypass -File
    "${PROJECT_SOURCE_DIR}/tools/watch-musical-state.ps1" -SelfTest)

# Developer benchmark only: timing is intentionally not a pass/fail CTest.
add_executable(prismforge_musical_state_benchmark MusicalStateBenchmark.cpp)
target_compile_definitions(prismforge_musical_state_benchmark PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(prismforge_musical_state_benchmark PRIVATE
  prismforge_core psapi)
if(MSVC)
  target_compile_options(prismforge_musical_state_benchmark PRIVATE
    /W4 /permissive-)
endif()

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

add_executable(prismforge_store_tests
  StoreTests.cpp
  ../engine/ShowStore.cpp)
target_include_directories(prismforge_store_tests PRIVATE ../engine)
target_compile_definitions(prismforge_store_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(prismforge_store_tests PRIVATE
  prismforge_core nlohmann_json::nlohmann_json shell32 ole32)
if(MSVC)
  target_compile_options(prismforge_store_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME prismforge_store_tests COMMAND prismforge_store_tests)

add_executable(prismforge_audio_tests
  AudioTests.cpp
  ../engine/AudioCapture.cpp
  ../engine/AudioSwitcher.cpp)
target_include_directories(prismforge_audio_tests PRIVATE
  ../engine
  $<TARGET_PROPERTY:PrismForge.Engine,INCLUDE_DIRECTORIES>)
target_compile_definitions(prismforge_audio_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(prismforge_audio_tests PRIVATE prismforge_core ole32)
if(MSVC)
  target_compile_options(prismforge_audio_tests PRIVATE /W4 /permissive-)
endif()
add_test(NAME prismforge_audio_tests COMMAND prismforge_audio_tests)
