add_library(pathguard_rules STATIC
  src/arrow_scanner.cpp
  src/compiler.cpp
  src/desugarer.cpp
  src/diagnostic.cpp
  src/format_probe.cpp
  src/semantic.cpp
  src/source.cpp
)

target_include_directories(pathguard_rules
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/tomlplusplus
)

target_compile_features(pathguard_rules PUBLIC cxx_std_20)
target_compile_definitions(pathguard_rules PRIVATE
  TOML_EXCEPTIONS=0
  TOML_ENABLE_FORMATTERS=0
  TOML_ENABLE_UNRELEASED_FEATURES=0
)

target_link_libraries(pathguard_rules PUBLIC pathguard_core)
