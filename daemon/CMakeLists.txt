add_library(pathguard_rules_control STATIC
  src/rules_control.cpp
)
target_include_directories(pathguard_rules_control PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(pathguard_rules_control PUBLIC
  pathguard_rules_compiler
)

add_executable(pathguardd src/main.cpp src/provenance_server.cpp)
target_link_libraries(pathguardd PRIVATE
  pathguard_core
  pathguard_rules_control
)
target_include_directories(pathguardd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
