add_library(pathguard_rules STATIC
  src/arrow_scanner.cpp
  src/format_probe.cpp
  src/source.cpp
)

target_include_directories(pathguard_rules
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(pathguard_rules PUBLIC cxx_std_20)
