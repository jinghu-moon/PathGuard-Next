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

add_executable(pathguard_policy_v6_test unit/policy_v6_test.cpp)
target_include_directories(pathguard_policy_v6_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_compile_definitions(pathguard_policy_v6_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(pathguard_policy_v6_test PRIVATE pathguard_core)
add_test(NAME pathguard_policy_v6_test COMMAND pathguard_policy_v6_test)

add_executable(pathguard_rules_pipeline_v6_test
  unit/rules_pipeline_v6_test.cpp)
target_include_directories(pathguard_rules_pipeline_v6_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(pathguard_rules_pipeline_v6_test PRIVATE
  pathguard_rules_compiler)
add_test(NAME pathguard_rules_pipeline_v6_test
  COMMAND pathguard_rules_pipeline_v6_test)

add_executable(pathguard_mount_transaction_test unit/mount_transaction_test.cpp)
target_link_libraries(pathguard_mount_transaction_test PRIVATE pathguard_core)
add_test(NAME pathguard_mount_transaction_test COMMAND pathguard_mount_transaction_test)

add_executable(pathguard_mount_backend_test unit/mount_backend_test.cpp)
target_link_libraries(pathguard_mount_backend_test PRIVATE pathguard_core)
add_test(NAME pathguard_mount_backend_test COMMAND pathguard_mount_backend_test)

add_test(NAME pathguard_provider_path_mapper_test
    COMMAND $<TARGET_FILE:pathguard_policy_action_router_test>)

add_executable(pathguard_provider_redirect_lifecycle_test
    unit/provider_redirect_lifecycle_test.cpp)
target_include_directories(pathguard_provider_redirect_lifecycle_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../zygisk/include)
add_test(NAME pathguard_provider_redirect_lifecycle_test
    COMMAND pathguard_provider_redirect_lifecycle_test)

add_executable(pathguard_provider_process_lifecycle_test
    unit/provider_process_lifecycle_test.cpp)
target_link_libraries(pathguard_provider_process_lifecycle_test PRIVATE
    pathguard_provider_process_lifecycle)
add_test(NAME pathguard_provider_process_lifecycle_test
    COMMAND pathguard_provider_process_lifecycle_test)

add_executable(pathguard_policy_snapshot_domain_test
    unit/policy_snapshot_domain_test.cpp)
target_include_directories(pathguard_policy_snapshot_domain_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../zygisk/include)
add_test(NAME pathguard_policy_snapshot_domain_test
    COMMAND pathguard_policy_snapshot_domain_test)

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
target_link_libraries(pathguard_runtime_status_test PRIVATE pathguard_rules_compiler)
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

add_executable(pathguard_rules_semantic_test
  unit/rules_semantic_test.cpp
)
target_include_directories(pathguard_rules_semantic_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_semantic_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_semantic_test COMMAND pathguard_rules_semantic_test)

add_executable(pathguard_rules_schema_v2_test
  unit/rules_schema_v2_test.cpp
)
target_include_directories(pathguard_rules_schema_v2_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_schema_v2_test PRIVATE
  pathguard_rules_compiler
)
add_test(NAME pathguard_rules_schema_v2_test
  COMMAND pathguard_rules_schema_v2_test)

add_executable(pathguard_rules_pattern_test
  unit/rules_pattern_test.cpp
)
target_include_directories(pathguard_rules_pattern_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_pattern_test PRIVATE
  pathguard_rules_compiler
)
add_test(NAME pathguard_rules_pattern_test COMMAND pathguard_rules_pattern_test)

add_executable(pathguard_rules_brace_test
  unit/rules_brace_test.cpp
)
target_include_directories(pathguard_rules_brace_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_brace_test PRIVATE
  pathguard_rules_compiler
)
add_test(NAME pathguard_rules_brace_test COMMAND pathguard_rules_brace_test)

add_executable(pathguard_selector_builder_test
  unit/selector_builder_test.cpp
)
target_include_directories(pathguard_selector_builder_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_selector_builder_test PRIVATE
  pathguard_rules_compiler
)
add_test(NAME pathguard_selector_builder_test
  COMMAND pathguard_selector_builder_test)

function(pathguard_add_runtime_test target source)
  add_executable(${target} ${source})
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
  )
  target_link_libraries(${target} PRIVATE pathguard_core)
  add_test(NAME ${target} COMMAND ${target})
endfunction()
pathguard_add_runtime_test(pathguard_candidate_index_test
  unit/candidate_index_test.cpp)
pathguard_add_runtime_test(pathguard_action_evaluator_test
  unit/action_evaluator_test.cpp)
pathguard_add_runtime_test(pathguard_action_admission_test
  unit/action_admission_test.cpp)
pathguard_add_runtime_test(pathguard_snapshot_publisher_test
  unit/snapshot_publisher_test.cpp)
add_executable(pathguard_mount_plan_adapter_test unit/mount_plan_adapter_test.cpp)
target_include_directories(pathguard_mount_plan_adapter_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_mount_plan_adapter_test PRIVATE
  pathguard_rules_compiler pathguard_core)
add_test(NAME pathguard_mount_plan_adapter_test
  COMMAND pathguard_mount_plan_adapter_test)
add_executable(pathguard_policy_pattern_runtime_test
  unit/policy_pattern_runtime_test.cpp)
target_include_directories(pathguard_policy_pattern_runtime_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_policy_pattern_runtime_test PRIVATE pathguard_core)
add_test(NAME pathguard_policy_pattern_runtime_test
  COMMAND pathguard_policy_pattern_runtime_test)
add_executable(pathguard_policy_action_router_test
  unit/policy_action_router_test.cpp)
target_include_directories(pathguard_policy_action_router_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_policy_action_router_test PRIVATE
  pathguard_rules_compiler pathguard_core)
add_test(NAME pathguard_policy_action_router_test
  COMMAND pathguard_policy_action_router_test)
add_executable(pathguard_path_hook_contract_test
  unit/path_hook_contract_test.cpp)
target_include_directories(pathguard_path_hook_contract_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_path_hook_contract_test PRIVATE pathguard_core)
add_test(NAME pathguard_path_hook_contract_test
  COMMAND pathguard_path_hook_contract_test)
add_executable(pathguard_route_provenance_test
  unit/route_provenance_test.cpp)
target_include_directories(pathguard_route_provenance_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_route_provenance_test PRIVATE pathguard_core)
add_test(NAME pathguard_route_provenance_test
  COMMAND pathguard_route_provenance_test)
add_executable(pathguard_provenance_broker_test
  unit/provenance_broker_test.cpp)
target_include_directories(pathguard_provenance_broker_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_provenance_broker_test PRIVATE pathguard_core)
add_test(NAME pathguard_provenance_broker_test
  COMMAND pathguard_provenance_broker_test)
add_executable(pathguard_failure_policy_test
  unit/failure_policy_test.cpp)
target_include_directories(pathguard_failure_policy_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_failure_policy_test PRIVATE pathguard_core)
add_test(NAME pathguard_failure_policy_test
  COMMAND pathguard_failure_policy_test)
add_executable(pathguard_provider_route_context_test
  unit/provider_route_context_test.cpp)
target_include_directories(pathguard_provider_route_context_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_provider_route_context_test PRIVATE pathguard_core)
add_test(NAME pathguard_provider_route_context_test
  COMMAND pathguard_provider_route_context_test)
add_executable(pathguard_provider_contract_test
  unit/provider_contract_test.cpp)
target_include_directories(pathguard_provider_contract_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(pathguard_provider_contract_test PRIVATE pathguard_core)
add_test(NAME pathguard_provider_contract_test
  COMMAND pathguard_provider_contract_test)
add_executable(pathguard_provider_adapter_profile_test
  unit/provider_adapter_profile_test.cpp)
target_include_directories(pathguard_provider_adapter_profile_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(pathguard_provider_adapter_profile_test PRIVATE pathguard_core)
add_test(NAME pathguard_provider_adapter_profile_test
  COMMAND pathguard_provider_adapter_profile_test)
pathguard_add_runtime_test(pathguard_operation_plan_test
  unit/operation_plan_test.cpp)
add_executable(pathguard_auxiliary_adapters_test
  unit/auxiliary_adapters_test.cpp)
target_include_directories(pathguard_auxiliary_adapters_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_auxiliary_adapters_test PRIVATE pathguard_core)
add_test(NAME pathguard_auxiliary_adapters_test
  COMMAND pathguard_auxiliary_adapters_test)

add_executable(pathguard_export_worker_test
  unit/export_worker_test.cpp)
target_include_directories(pathguard_export_worker_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_export_worker_test PRIVATE pathguard_core)
add_test(NAME pathguard_export_worker_test
  COMMAND pathguard_export_worker_test)

add_executable(pathguard_rules_effects_test unit/rules_effects_test.cpp)
target_include_directories(pathguard_rules_effects_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathguard_rules_effects_test PRIVATE
  pathguard_rules_compiler pathguard_core)
add_test(NAME pathguard_rules_effects_test COMMAND pathguard_rules_effects_test)

add_test(NAME pathguard_cli_v6_integration
  COMMAND ${CMAKE_COMMAND}
    -DPATHGUARDCTL=$<TARGET_FILE:pathguardctl>
    -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_v6_test.cmake)

add_executable(pathguard_rules_conflict_test
  unit/rules_conflict_test.cpp
)
target_include_directories(pathguard_rules_conflict_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_link_libraries(pathguard_rules_conflict_test PRIVATE pathguard_rules_compiler)
add_test(NAME pathguard_rules_conflict_test COMMAND pathguard_rules_conflict_test)

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
add_test(NAME pathguard_rules_migration_test
  COMMAND pathguard_rules_migration_test)

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
  foreach(target IN ITEMS compile)
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
  function(pathguard_add_pattern_fuzzer target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
      ${CMAKE_SOURCE_DIR}/core/include
      ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
    )
    target_compile_options(${target} PRIVATE
      -fsanitize=${pathguard_fuzzer_sanitizers}
      -fno-omit-frame-pointer
    )
    target_link_options(${target} PRIVATE
      -fsanitize=${pathguard_fuzzer_sanitizers}
    )
    target_link_libraries(${target} PRIVATE pathguard_rules_compiler)
    if(WIN32)
      target_compile_options(${target} PRIVATE
        -fms-runtime-lib=static
      )
      target_link_options(${target} PRIVATE
        -fms-runtime-lib=static
      )
    endif()
    set_property(TARGET ${target} PROPERTY
      MSVC_RUNTIME_LIBRARY MultiThreaded)
  endfunction()
  pathguard_add_pattern_fuzzer(pathguard_pattern_tokenizer_fuzzer
    fuzz/pattern_tokenizer_fuzzer.cpp)
  pathguard_add_pattern_fuzzer(pathguard_pattern_matcher_fuzzer
    fuzz/pattern_matcher_fuzzer.cpp)
  if(WIN32)
    target_compile_options(pathguard_rules_compiler PRIVATE -fms-runtime-lib=static)
    target_compile_options(pathguard_core PRIVATE -fms-runtime-lib=static)
  endif()
  set_property(TARGET pathguard_rules_compiler PROPERTY
    MSVC_RUNTIME_LIBRARY MultiThreaded)
  set_property(TARGET pathguard_core PROPERTY
    MSVC_RUNTIME_LIBRARY MultiThreaded)
endif()

add_executable(pathguard_pattern_fuzz_smoke
  fuzz/pattern_fuzz_smoke.cpp
)
target_include_directories(pathguard_pattern_fuzz_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
)
target_compile_definitions(pathguard_pattern_fuzz_smoke PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_pattern_fuzz_smoke PRIVATE
  pathguard_rules_compiler)
add_test(NAME pathguard_pattern_fuzz_smoke
  COMMAND pathguard_pattern_fuzz_smoke)

add_executable(pathguard_pattern_harness_contract_test
  unit/pattern_harness_contract_test.cpp
)
target_include_directories(pathguard_pattern_harness_contract_test PRIVATE
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
)
target_compile_definitions(pathguard_pattern_harness_contract_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_pattern_harness_contract_test PRIVATE
  pathguard_rules_compiler)
add_test(NAME pathguard_pattern_harness_contract
  COMMAND pathguard_pattern_harness_contract_test)

add_executable(pathguard_pattern_property_smoke_test
  unit/pattern_property_smoke_test.cpp
)
target_include_directories(pathguard_pattern_property_smoke_test PRIVATE
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
)
target_compile_definitions(pathguard_pattern_property_smoke_test PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_pattern_property_smoke_test PRIVATE
  pathguard_rules_compiler)
add_test(NAME pathguard_pattern_property_smoke
  COMMAND pathguard_pattern_property_smoke_test)

add_executable(pathguard_pattern_benchmark
  perf/pattern_benchmark.cpp
)
target_include_directories(pathguard_pattern_benchmark PRIVATE
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
)
target_compile_definitions(pathguard_pattern_benchmark PRIVATE
  PATHGUARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(pathguard_pattern_benchmark PRIVATE
  pathguard_rules_compiler)
add_test(NAME pathguard_pattern_benchmark_jsonl
  COMMAND pathguard_pattern_benchmark --format=jsonl)
add_test(NAME pathguard_pattern_benchmark_tsv
  COMMAND pathguard_pattern_benchmark --format=tsv)

add_executable(pathguard_runtime_benchmark
  perf/runtime_benchmark.cpp)
target_include_directories(pathguard_runtime_benchmark PRIVATE
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_SOURCE_DIR}/rules/include)
target_link_libraries(pathguard_runtime_benchmark PRIVATE
  pathguard_rules_compiler pathguard_core)
if(WIN32)
  target_link_libraries(pathguard_runtime_benchmark PRIVATE Psapi)
endif()
add_test(NAME pathguard_runtime_benchmark
  COMMAND pathguard_runtime_benchmark)
add_test(NAME pathguard_runtime_benchmark_json
  COMMAND ${CMAKE_COMMAND}
    "-DBENCHMARK=$<TARGET_FILE:pathguard_runtime_benchmark>"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/perf/runtime_benchmark_json_test.cmake)
add_test(NAME pathguard_runtime_soak_smoke
  COMMAND pathguard_runtime_benchmark --soak-seconds=2)

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

add_test(
  NAME pathguard_rules_control_plane_integration
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DPATHGUARDCTL=$<TARGET_FILE:pathguardctl>
    -DPATHGUARDD=$<TARGET_FILE:pathguardd>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/rules/control_plane_test.cmake
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
  NAME pathguard_comparison_report_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/comparison_report_guard_test.cmake
)

add_test(
  NAME pathguard_pattern_harness_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/pattern_harness_guard_test.cmake
)

add_test(
  NAME pathguard_pattern_core_p0_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/pattern_core_p0_guard_test.cmake
)

add_test(
  NAME pathguard_pattern_runtime_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/pattern_runtime_guard_test.cmake
)

add_test(
  NAME pathguard_policy_v6_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/policy_v6_guard_test.cmake
)

add_test(
  NAME pathguard_legacy_cutover_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/pattern-v6/legacy-cutover-guard.cmake
)

add_test(
  NAME pathguard_production_integration_guard
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/baseline/pattern-v6/production-integration-guard.cmake
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
