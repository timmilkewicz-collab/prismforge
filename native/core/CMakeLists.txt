add_library(prismforge_core STATIC
  src/SignalAnalyzer.cpp
  src/ShowState.cpp
  src/QualityGovernor.cpp
)
target_include_directories(prismforge_core PUBLIC include)
target_compile_features(prismforge_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(prismforge_core PRIVATE /W4 /permissive-)
endif()
