add_library(pathguard_core STATIC
  src/binary.cpp
  src/path.cpp
  src/policy.cpp
  src/validation.cpp
  src/version.cpp
)

target_include_directories(pathguard_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(pathguard_core PUBLIC cxx_std_20)
