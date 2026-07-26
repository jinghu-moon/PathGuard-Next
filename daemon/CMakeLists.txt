add_executable(pathguardd
  src/main.cpp
  src/legacy_rules_control.cpp
)
target_include_directories(pathguardd PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(pathguardd PRIVATE pathguard_core)
