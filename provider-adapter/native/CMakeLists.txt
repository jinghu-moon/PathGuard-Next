cmake_minimum_required(VERSION 3.28)
project(pathguard_lsplant_bridge LANGUAGES C CXX ASM)

if(NOT ANDROID)
    message(FATAL_ERROR "pathguard_lsplant_bridge is Android-only")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

get_filename_component(PATHGUARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(PATHGUARD_THIRD_PARTY "${PATHGUARD_ROOT}/third_party")

set(PATHGUARD_DOBBY_AAR
    "${PATHGUARD_THIRD_PARTY}/dobby/artifacts/dobby-1.2.aar")
set(PATHGUARD_DOBBY_EXTRACTED "${CMAKE_BINARY_DIR}/dobby-1.2")
if(NOT EXISTS "${PATHGUARD_DOBBY_AAR}")
    message(FATAL_ERROR "pinned Dobby AAR is missing")
endif()
file(MAKE_DIRECTORY "${PATHGUARD_DOBBY_EXTRACTED}")
file(ARCHIVE_EXTRACT INPUT "${PATHGUARD_DOBBY_AAR}"
    DESTINATION "${PATHGUARD_DOBBY_EXTRACTED}")
add_library(pathguard_dobby STATIC IMPORTED GLOBAL)
set_target_properties(pathguard_dobby PROPERTIES
    IMPORTED_LOCATION
        "${PATHGUARD_DOBBY_EXTRACTED}/prefab/modules/dobby/libs/android.${ANDROID_ABI}/libdobby.a"
    INTERFACE_INCLUDE_DIRECTORIES
        "${PATHGUARD_DOBBY_EXTRACTED}/prefab/modules/dobby/include")

file(GLOB PATHGUARD_XDL_SOURCES CONFIGURE_DEPENDS
    "${PATHGUARD_THIRD_PARTY}/xdl/src/*.c")
add_library(pathguard_xdl STATIC ${PATHGUARD_XDL_SOURCES})
target_compile_features(pathguard_xdl PUBLIC c_std_17)
target_compile_options(pathguard_xdl PRIVATE
    -Os -ffunction-sections -fdata-sections)
target_include_directories(pathguard_xdl PUBLIC
    "${PATHGUARD_THIRD_PARTY}/xdl/src/include"
    "${PATHGUARD_THIRD_PARTY}/xdl/src")

set(LSPLANT_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(DEBUG_SYMBOLS_PATH "${CMAKE_BINARY_DIR}/symbols")
add_subdirectory("${PATHGUARD_THIRD_PARTY}/lsplant/src" lsplant)

add_library(pathguard_lsplant SHARED
    src/provider_lsplant_bridge.cpp
    "${PATHGUARD_ROOT}/core/src/provider_mapping.cpp"
    "${PATHGUARD_ROOT}/core/src/route_provenance.cpp")
target_include_directories(pathguard_lsplant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PATHGUARD_ROOT}/core/include"
    "${PATHGUARD_DOBBY_EXTRACTED}/prefab/modules/dobby/include"
    "${PATHGUARD_THIRD_PARTY}/lsplant/src/include")
target_compile_options(pathguard_lsplant PRIVATE
    -Wall -Wextra -Werror -fvisibility=hidden -fvisibility-inlines-hidden)
target_link_options(pathguard_lsplant PRIVATE
    -Wl,--exclude-libs,ALL
    -Wl,--gc-sections
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/pathguard_lsplant.map.txt
    -Wl,-z,max-page-size=16384)
target_link_libraries(pathguard_lsplant PRIVATE
    lsplant_static pathguard_dobby pathguard_xdl log dl)
