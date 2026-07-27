add_executable(pathguard_core_smoke
  unit/core_smoke_test.cpp
)

target_link_libraries(pathguard_core_smoke PRIVATE pathguard_core)
add_test(NAME pathguard_core_smoke COMMAND pathguard_core_smoke)

add_executable(pathguard_policy_test
  unit/policy_test.cpp
)

target_compile_definitions(pathguard_policy_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_policy_test PRIVATE pathguard_core)
add_test(NAME pathguard_policy_test COMMAND pathguard_policy_test)

add_executable(pathguard_legacy_policy_characterization_test
  unit/legacy_policy_characterization_test.cpp
)
target_compile_definitions(pathguard_legacy_policy_characterization_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_legacy_policy_characterization_test PRIVATE pathguard_core)
add_test(
  NAME pathguard_legacy_policy_characterization_test
  COMMAND pathguard_legacy_policy_characterization_test
)

add_executable(pathguard_path_test
  unit/path_test.cpp
)

target_link_libraries(pathguard_path_test PRIVATE pathguard_core)
add_test(NAME pathguard_path_test COMMAND pathguard_path_test)

add_executable(pathguard_validation_test unit/validation_test.cpp)
target_link_libraries(pathguard_validation_test PRIVATE pathguard_core)
add_test(NAME pathguard_validation_test COMMAND pathguard_validation_test)

add_executable(pathguard_binary_test unit/binary_test.cpp)
target_compile_definitions(pathguard_binary_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_binary_test PRIVATE pathguard_core)
add_test(NAME pathguard_binary_test COMMAND pathguard_binary_test)

add_executable(pathguard_legacy_rules_control_test
  unit/legacy_rules_control_test.cpp
  ../daemon/src/legacy_rules_control.cpp
)
target_compile_definitions(pathguard_legacy_rules_control_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_include_directories(pathguard_legacy_rules_control_test PRIVATE
  ${CMAKE_SOURCE_DIR}/daemon/include
)
target_link_libraries(pathguard_legacy_rules_control_test PRIVATE pathguard_core)
add_test(
  NAME pathguard_legacy_rules_control_test
  COMMAND pathguard_legacy_rules_control_test
)

add_executable(pathguard_mount_transaction_test unit/mount_transaction_test.cpp)
target_link_libraries(pathguard_mount_transaction_test PRIVATE pathguard_core)
add_test(NAME pathguard_mount_transaction_test COMMAND pathguard_mount_transaction_test)

add_executable(pathguard_mount_backend_test unit/mount_backend_test.cpp)
target_link_libraries(pathguard_mount_backend_test PRIVATE pathguard_core)
add_test(NAME pathguard_mount_backend_test COMMAND pathguard_mount_backend_test)

add_executable(pathguard_provider_path_mapper_test
    unit/provider_path_mapper_test.cpp
    ../zygisk/src/provider_path_mapper.cpp)
target_include_directories(pathguard_provider_path_mapper_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../zygisk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/unit)
add_test(NAME pathguard_provider_path_mapper_test COMMAND pathguard_provider_path_mapper_test)

add_executable(pathguard_topology_test unit/topology_test.cpp)
target_link_libraries(pathguard_topology_test PRIVATE pathguard_core)
add_test(NAME pathguard_topology_test COMMAND pathguard_topology_test)

add_executable(pathguard_runtime_status_test unit/runtime_status_test.cpp)
target_link_libraries(pathguard_runtime_status_test PRIVATE pathguard_core)
add_test(NAME pathguard_runtime_status_test COMMAND pathguard_runtime_status_test)

add_executable(pathguard_rules_contract_test unit/rules_contract_test.cpp)
target_include_directories(pathguard_rules_contract_test PRIVATE
  ${CMAKE_SOURCE_DIR}/rules/include
)
add_test(NAME pathguard_rules_contract_test COMMAND pathguard_rules_contract_test)

add_executable(pathguard_policy_benchmark perf/policy_benchmark.cpp)
target_link_libraries(pathguard_policy_benchmark PRIVATE pathguard_core)

add_executable(pathguard_cpp_toml_adapter_spike
  d0/cpp_toml_adapter_spike.cpp
)
target_include_directories(pathguard_cpp_toml_adapter_spike PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/tomlplusplus
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(pathguard_cpp_toml_adapter_spike PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(
  NAME pathguard_cpp_toml_adapter_spike
  COMMAND pathguard_cpp_toml_adapter_spike
)
target_link_libraries(pathguard_cpp_toml_adapter_spike PRIVATE pathguard_core)

add_executable(pathguard_toml_conformance_subset_test
  d0/toml_conformance_subset_test.cpp
)
target_include_directories(pathguard_toml_conformance_subset_test PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/tomlplusplus
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(pathguard_toml_conformance_subset_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(
  NAME pathguard_toml_conformance_subset_test
  COMMAND pathguard_toml_conformance_subset_test
)

add_executable(pathguard_cpp_d0_benchmark
  d0/cpp_d0_benchmark.cpp
)
target_include_directories(pathguard_cpp_d0_benchmark PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/tomlplusplus
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_cpp_d0_benchmark PRIVATE pathguard_core)

add_test(
  NAME pathguard_legacy_control_plane_integration
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DPATHGUARDCTL=$<TARGET_FILE:pathguardctl>
    -DPATHGUARDD=$<TARGET_FILE:pathguardd>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/legacy_control_plane_test.cmake
)

add_test(
  NAME pathguard_test_asset_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/test_asset_guard_test.cmake
)

add_test(
  NAME pathguard_rules_d0_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/rules_d0_guard_test.cmake
)

add_test(
  NAME pathguard_host_baseline_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/host_baseline_guard_test.cmake
)
