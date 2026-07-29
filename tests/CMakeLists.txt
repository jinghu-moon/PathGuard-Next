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

add_executable(pathguard_provider_redirect_lifecycle_test
    unit/provider_redirect_lifecycle_test.cpp)
target_include_directories(pathguard_provider_redirect_lifecycle_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../zygisk/include)
add_test(NAME pathguard_provider_redirect_lifecycle_test
    COMMAND pathguard_provider_redirect_lifecycle_test)

add_executable(pathguard_provider_caller_uid_test
    unit/provider_caller_uid_test.cpp)
target_include_directories(pathguard_provider_caller_uid_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../zygisk/include)
add_test(NAME pathguard_provider_caller_uid_test
    COMMAND pathguard_provider_caller_uid_test)

add_executable(pathguard_media_query_filter_test
    unit/media_query_filter_test.cpp
    ../zygisk/src/media_query_filter.cpp)
target_include_directories(pathguard_media_query_filter_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../zygisk/include)
add_test(NAME pathguard_media_query_filter_test COMMAND pathguard_media_query_filter_test)

add_executable(pathguard_topology_test unit/topology_test.cpp)
target_link_libraries(pathguard_topology_test PRIVATE pathguard_core)
add_test(NAME pathguard_topology_test COMMAND pathguard_topology_test)

add_executable(pathguard_runtime_status_test unit/runtime_status_test.cpp)
target_link_libraries(pathguard_runtime_status_test PRIVATE pathguard_core)
add_test(NAME pathguard_runtime_status_test COMMAND pathguard_runtime_status_test)

add_executable(pathguard_hide_probe_contract_test
  unit/hide_probe_contract_test.cpp
  device/hide/hide_probe_contract.cpp
)
target_include_directories(pathguard_hide_probe_contract_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/device/hide
)
add_test(NAME pathguard_hide_probe_contract_test
  COMMAND pathguard_hide_probe_contract_test)

add_executable(pathguard_rules_contract_test unit/rules_contract_test.cpp)
target_include_directories(pathguard_rules_contract_test PRIVATE
  ${CMAKE_SOURCE_DIR}/rules/include
)
add_test(NAME pathguard_rules_contract_test COMMAND pathguard_rules_contract_test)

add_executable(pathguard_rules_source_test unit/rules_source_test.cpp)
target_include_directories(pathguard_rules_source_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_source_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_source_test COMMAND pathguard_rules_source_test)

add_executable(pathguard_rules_format_probe_test unit/rules_format_probe_test.cpp)
target_include_directories(pathguard_rules_format_probe_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_format_probe_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_format_probe_test COMMAND pathguard_rules_format_probe_test)

add_executable(pathguard_rules_arrow_scanner_test
  unit/rules_arrow_scanner_test.cpp
)
target_include_directories(pathguard_rules_arrow_scanner_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_arrow_scanner_test PRIVATE pathguard_rules_compiler)
add_test(
  NAME pathguard_rules_arrow_scanner_test
  COMMAND pathguard_rules_arrow_scanner_test
)

add_executable(pathguard_rules_fixture_scanner_test
  unit/rules_fixture_scanner_test.cpp
)
target_include_directories(pathguard_rules_fixture_scanner_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(pathguard_rules_fixture_scanner_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_rules_fixture_scanner_test PRIVATE pathguard_rules_compiler)
add_test(
  NAME pathguard_rules_fixture_scanner_test
  COMMAND pathguard_rules_fixture_scanner_test
)

add_executable(pathguard_rules_string_fuzz_smoke
  fuzz/rules_string_fuzz_smoke.cpp
)
target_include_directories(pathguard_rules_string_fuzz_smoke PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(pathguard_rules_string_fuzz_smoke PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_rules_string_fuzz_smoke PRIVATE pathguard_rules_compiler)
add_test(
  NAME pathguard_rules_string_fuzz_smoke
  COMMAND pathguard_rules_string_fuzz_smoke
)

add_executable(pathguard_rules_candidate_fuzz_smoke
  fuzz/rules_candidate_fuzz_smoke.cpp
)
target_include_directories(pathguard_rules_candidate_fuzz_smoke PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(pathguard_rules_candidate_fuzz_smoke PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_rules_candidate_fuzz_smoke PRIVATE pathguard_rules_compiler)
add_test(
  NAME pathguard_rules_candidate_fuzz_smoke
  COMMAND pathguard_rules_candidate_fuzz_smoke
)

add_executable(pathguard_rules_desugarer_test
  unit/rules_desugarer_test.cpp
)
target_include_directories(pathguard_rules_desugarer_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_desugarer_test PRIVATE pathguard_rules_compiler)
add_test(
  NAME pathguard_rules_desugarer_test
  COMMAND pathguard_rules_desugarer_test
)

add_executable(pathguard_rules_desugarer_golden_test
  unit/rules_desugarer_golden_test.cpp
)
target_include_directories(pathguard_rules_desugarer_golden_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(pathguard_rules_desugarer_golden_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_rules_desugarer_golden_test PRIVATE pathguard_rules_compiler)
add_test(
  NAME pathguard_rules_desugarer_golden_test
  COMMAND pathguard_rules_desugarer_golden_test
)

add_executable(pathguard_rules_desugar_fuzz_smoke
  fuzz/rules_desugar_fuzz_smoke.cpp
)
target_include_directories(pathguard_rules_desugar_fuzz_smoke PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(pathguard_rules_desugar_fuzz_smoke PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_rules_desugar_fuzz_smoke PRIVATE pathguard_rules_compiler)
add_test(
  NAME pathguard_rules_desugar_fuzz_smoke
  COMMAND pathguard_rules_desugar_fuzz_smoke
)

add_executable(pathguard_rules_parser_test
  unit/rules_parser_test.cpp
)
target_include_directories(pathguard_rules_parser_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_parser_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_parser_test COMMAND pathguard_rules_parser_test)

add_executable(pathguard_rules_document_test
  unit/rules_document_test.cpp
)
target_include_directories(pathguard_rules_document_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_document_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_document_test COMMAND pathguard_rules_document_test)

add_executable(pathguard_rules_semantic_test
  unit/rules_semantic_test.cpp
)
target_include_directories(pathguard_rules_semantic_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_semantic_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_semantic_test COMMAND pathguard_rules_semantic_test)

add_executable(pathguard_rules_conflict_test
  unit/rules_conflict_test.cpp
)
target_include_directories(pathguard_rules_conflict_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_conflict_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_conflict_test COMMAND pathguard_rules_conflict_test)

add_executable(pathguard_rules_pipeline_test
  unit/rules_pipeline_test.cpp
)
target_include_directories(pathguard_rules_pipeline_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(pathguard_rules_pipeline_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_rules_pipeline_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_pipeline_test COMMAND pathguard_rules_pipeline_test)

add_executable(pathguard_rules_tools_test unit/rules_tools_test.cpp)
target_include_directories(pathguard_rules_tools_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_tools_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_tools_test COMMAND pathguard_rules_tools_test)

add_executable(pathguard_rules_limits_test unit/rules_limits_test.cpp)
target_include_directories(pathguard_rules_limits_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_limits_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_limits_test COMMAND pathguard_rules_limits_test)

add_executable(pathguard_rules_compile_fuzz_smoke
  fuzz/rules_compile_fuzz_smoke.cpp
)
target_include_directories(pathguard_rules_compile_fuzz_smoke PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_compile_fuzz_smoke PRIVATE
  pathguard_rules_compiler
)
add_test(NAME pathguard_rules_compile_fuzz_smoke
  COMMAND pathguard_rules_compile_fuzz_smoke)

add_executable(pathguard_rules_control_test
  unit/rules_control_test.cpp
)
target_include_directories(pathguard_rules_control_test PRIVATE
  ${CMAKE_SOURCE_DIR}/daemon/include
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_control_test PRIVATE
  pathguard_rules_control
)
add_test(NAME pathguard_rules_control_test COMMAND pathguard_rules_control_test)

add_executable(pathguard_rules_manager_save_test
  unit/rules_manager_save_test.cpp
)
target_include_directories(pathguard_rules_manager_save_test PRIVATE
  ${CMAKE_SOURCE_DIR}/daemon/include
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_manager_save_test PRIVATE
  pathguard_rules_control
)
add_test(NAME pathguard_rules_manager_save_test
  COMMAND pathguard_rules_manager_save_test)

add_executable(pathguard_rules_content_generation_test
  unit/rules_content_generation_test.cpp
)
target_include_directories(pathguard_rules_content_generation_test PRIVATE
  ${CMAKE_SOURCE_DIR}/daemon/include
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_content_generation_test PRIVATE
  pathguard_rules_control
)
add_test(NAME pathguard_rules_content_generation_test
  COMMAND pathguard_rules_content_generation_test)

add_executable(pathguard_rules_migration_test
  unit/rules_migration_test.cpp
)
target_compile_definitions(pathguard_rules_migration_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_include_directories(pathguard_rules_migration_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_migration_test PRIVATE
  pathguard_rules_compiler
)
add_test(NAME pathguard_rules_migration_test COMMAND pathguard_rules_migration_test)

add_test(
  NAME pathguard_rules_format_cutover_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/rules_format_cutover_guard_test.cmake
)

add_test(
  NAME pathguard_rules_release_audit_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/rules_release_audit_guard_test.cmake
)

add_executable(pathguard_rules_parity_probe
  integration/rules/rules_compiler_parity_probe.cpp
)
target_link_libraries(pathguard_rules_parity_probe PRIVATE
  pathguard_rules_compiler
)
add_test(NAME pathguard_rules_parity_probe COMMAND pathguard_rules_parity_probe)

add_test(
  NAME pathguard_rules_build_boundary_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/rules_build_boundary_guard_test.cmake
)

add_test(
  NAME pathguard_zygisk_elf_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/zygisk_elf_guard_test.cmake
)

add_test(
  NAME pathguard_post_fs_bootstrap_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/post_fs_bootstrap_guard_test.cmake
)

add_test(
  NAME pathguard_native_toolchain_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/native_toolchain_guard_test.cmake
)

if(PATHGUARD_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "PATHGUARD_BUILD_FUZZERS requires Clang/libFuzzer")
  endif()
  if(WIN32)
    set(pathguard_fuzzer_sanitizers fuzzer,undefined)
    set(pathguard_library_sanitizers fuzzer-no-link,undefined)
  else()
    set(pathguard_fuzzer_sanitizers fuzzer,address,undefined)
    set(pathguard_library_sanitizers fuzzer-no-link,address,undefined)
  endif()
  target_compile_options(pathguard_rules_compiler PRIVATE
    -fsanitize=${pathguard_library_sanitizers}
    -fno-omit-frame-pointer
  )
  foreach(target IN ITEMS string candidate desugar compile)
    add_executable(pathguard_rules_${target}_fuzzer
      fuzz/rules_${target}_fuzzer.cpp
    )
    target_link_libraries(pathguard_rules_${target}_fuzzer PRIVATE pathguard_rules_compiler)
    target_compile_options(pathguard_rules_${target}_fuzzer PRIVATE
      -fsanitize=${pathguard_fuzzer_sanitizers}
      -fno-omit-frame-pointer
    )
    target_link_options(pathguard_rules_${target}_fuzzer PRIVATE
      -fsanitize=${pathguard_fuzzer_sanitizers}
    )
    if(WIN32)
      target_compile_options(pathguard_rules_${target}_fuzzer PRIVATE
        -fms-runtime-lib=static
      )
      target_link_options(pathguard_rules_${target}_fuzzer PRIVATE
        -fms-runtime-lib=static
      )
    endif()
    set_property(TARGET pathguard_rules_${target}_fuzzer PROPERTY
      MSVC_RUNTIME_LIBRARY MultiThreaded)
  endforeach()
  if(WIN32)
    target_compile_options(pathguard_rules_compiler PRIVATE -fms-runtime-lib=static)
    target_compile_options(pathguard_core PRIVATE -fms-runtime-lib=static)
  endif()
  set_property(TARGET pathguard_rules_compiler PROPERTY
    MSVC_RUNTIME_LIBRARY MultiThreaded)
  set_property(TARGET pathguard_core PROPERTY
    MSVC_RUNTIME_LIBRARY MultiThreaded)
endif()

add_executable(pathguard_policy_benchmark perf/policy_benchmark.cpp)
target_include_directories(pathguard_policy_benchmark PRIVATE
  ${CMAKE_SOURCE_DIR}/daemon/include
)
target_link_libraries(pathguard_policy_benchmark PRIVATE
  pathguard_rules_control
)
if(WIN32)
  target_link_libraries(pathguard_policy_benchmark PRIVATE Psapi)
endif()
add_test(NAME pathguard_rules_compiler_budget COMMAND pathguard_policy_benchmark)

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
  NAME pathguard_rules_control_plane_integration
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DPATHGUARDCTL=$<TARGET_FILE:pathguardctl>
    -DPATHGUARDD=$<TARGET_FILE:pathguardd>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/control_plane_test.cmake
)

add_test(
  NAME pathguard_rules_tools_cli_integration
  COMMAND ${CMAKE_COMMAND}
    -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DPATHGUARDCTL=$<TARGET_FILE:pathguardctl>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/rules_tools_cli_test.cmake
)

if(WIN32)
  find_program(PATHGUARD_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
  add_test(
    NAME pathguard_rules_hot_reload_integration
    COMMAND ${PATHGUARD_POWERSHELL_EXECUTABLE} -NoProfile -File
      ${CMAKE_SOURCE_DIR}/tests/hot_reload_test.ps1
      -Daemon $<TARGET_FILE:pathguardd>
  )
endif()

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
