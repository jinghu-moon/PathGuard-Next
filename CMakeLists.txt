cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0091 NEW)
project(PathGuardNext LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PATHGUARD_BUILD_TESTS "Build host tests" ON)
option(PATHGUARD_BUILD_FUZZERS "Build Clang/libFuzzer rules scanner targets" OFF)

add_subdirectory(core)
add_subdirectory(rules)
add_subdirectory(daemon)
add_subdirectory(cli)

add_library(pathguard_hide_loader STATIC
  experimental/hide-vfs/loader/offline_module_adapter.cpp
  experimental/hide-vfs/loader/restricted_loader.cpp
)
target_include_directories(pathguard_hide_loader PUBLIC
  ${CMAKE_SOURCE_DIR}/experimental/hide-vfs/loader
)
target_compile_features(pathguard_hide_loader PUBLIC cxx_std_20)

add_library(pathguard_hide_vfs_model STATIC
  experimental/hide-vfs/model/hide_vfs_model.c
)
target_include_directories(pathguard_hide_vfs_model PUBLIC
  ${CMAKE_SOURCE_DIR}/experimental/hide-vfs/model
)
target_compile_features(pathguard_hide_vfs_model PUBLIC c_std_11)

add_executable(pathguard_lkm_offline_adapter
  experimental/hide-vfs/loader/offline_adapter_main.cpp
)
target_link_libraries(pathguard_lkm_offline_adapter PRIVATE pathguard_hide_loader)

add_executable(pathguard_lkm_loader
  experimental/hide-vfs/loader/restricted_loader_main.cpp
)
target_link_libraries(pathguard_lkm_loader PRIVATE pathguard_hide_loader)

if(PATHGUARD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
