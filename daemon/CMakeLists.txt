add_library(pathguard_rules_control STATIC
  src/rules_control.cpp
)
target_include_directories(pathguard_rules_control PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(pathguard_rules_control PUBLIC
  pathguard_rules_compiler
)

add_library(pathguard_provider_process_lifecycle STATIC
  src/provider_process_lifecycle.cpp
)
target_include_directories(pathguard_provider_process_lifecycle PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(pathguard_provider_process_lifecycle PUBLIC
  pathguard_core
)

add_executable(pathguardd src/main.cpp src/audit_server.cpp)
target_link_libraries(pathguardd PRIVATE
  pathguard_core
  pathguard_provider_process_lifecycle
  pathguard_rules_control
)
target_include_directories(pathguardd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
