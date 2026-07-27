cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0091 NEW)
project(PathGuardNext LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PATHGUARD_BUILD_TESTS "Build host tests" ON)
option(PATHGUARD_BUILD_FUZZERS "Build Clang/libFuzzer rules scanner targets" OFF)

add_subdirectory(core)
add_subdirectory(rules)
add_subdirectory(daemon)
add_subdirectory(cli)

if(PATHGUARD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
