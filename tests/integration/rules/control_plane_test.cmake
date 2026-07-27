if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR
   OR NOT DEFINED PATHGUARDCTL OR NOT DEFINED PATHGUARDD)
  message(FATAL_ERROR
    "SOURCE_DIR, BINARY_DIR, PATHGUARDCTL and PATHGUARDD are required")
endif()

set(root "${BINARY_DIR}/rf7-control-plane")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}")
set(valid "${SOURCE_DIR}/tests/fixtures/rules/migrated-valid.toml")
set(invalid "${SOURCE_DIR}/tests/fixtures/rules/invalid.toml")

execute_process(
  COMMAND "${PATHGUARDCTL}" validate
    "${SOURCE_DIR}/module/config/rules.toml" --host
  RESULT_VARIABLE template_result
  ERROR_VARIABLE template_error
)
if(NOT template_result EQUAL 0)
  message(FATAL_ERROR "module rules.toml template is invalid: ${template_error}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}"
  RESULT_VARIABLE usage_result
  ERROR_VARIABLE usage_error
)
if(NOT usage_result EQUAL 2 OR NOT usage_error MATCHES "rules.toml"
   OR usage_error MATCHES "rules.ini")
  message(FATAL_ERROR "CLI format 1 usage contract failed: ${usage_error}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" validate "${valid}" --host
  RESULT_VARIABLE validate_result
  OUTPUT_VARIABLE validate_output
  ERROR_VARIABLE validate_error
)
if(NOT validate_result EQUAL 0 OR NOT validate_output MATCHES "valid: 1 package")
  message(FATAL_ERROR
    "CLI validation failed: ${validate_output} ${validate_error}")
endif()

set(cli_policy "${root}/offline-policy.bin")
execute_process(
  COMMAND "${PATHGUARDCTL}" compile "${valid}" "${cli_policy}"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_output
  ERROR_VARIABLE compile_error
)
if(NOT compile_result EQUAL 0 OR NOT EXISTS "${cli_policy}")
  message(FATAL_ERROR
    "CLI offline compile failed: ${compile_output} ${compile_error}")
endif()

file(MAKE_DIRECTORY "${root}/run")
execute_process(
  COMMAND "${PATHGUARDCTL}" compile "${valid}" "${root}/run/policy.bin"
  RESULT_VARIABLE active_write_result
  ERROR_VARIABLE active_write_error
)
if(active_write_result EQUAL 0 OR NOT active_write_error MATCHES "use reload")
  message(FATAL_ERROR "CLI wrote an active policy path")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" validate "${invalid}" --host --json
  RESULT_VARIABLE invalid_result
  ERROR_VARIABLE invalid_error
)
if(invalid_result EQUAL 0 OR NOT invalid_error MATCHES "PG-")
  message(FATAL_ERROR "CLI JSON diagnostic failed: ${invalid_error}")
endif()
execute_process(
  COMMAND "${PATHGUARDCTL}" validate "${invalid}" --host
  RESULT_VARIABLE invalid_text_result
  ERROR_VARIABLE invalid_text_error
)
if(invalid_text_result EQUAL 0 OR NOT invalid_text_error MATCHES "PG-ARROW-OPERAND"
   OR NOT invalid_error MATCHES "PG-ARROW-OPERAND")
  message(FATAL_ERROR "CLI text/JSON diagnostic codes diverged")
endif()
execute_process(
  COMMAND "${PATHGUARDCTL}" validate "${valid}" --device
  RESULT_VARIABLE device_result
  ERROR_VARIABLE device_error
)
if(device_result EQUAL 0 OR NOT device_error MATCHES "environment_unsupported")
  message(FATAL_ERROR "CLI device validation bypassed daemon admission")
endif()

set(module "${root}/module")
file(MAKE_DIRECTORY "${module}/config" "${module}/run")
file(COPY_FILE "${valid}" "${module}/config/rules.toml")
execute_process(
  COMMAND "${PATHGUARDD}" --module-dir "${module}" --compile
  RESULT_VARIABLE daemon_first_result
  OUTPUT_VARIABLE daemon_first_output
  ERROR_VARIABLE daemon_first_error
)
if(NOT daemon_first_result EQUAL 0
   OR NOT daemon_first_output MATCHES "published=1"
   OR NOT EXISTS "${module}/run/policy.bin"
   OR NOT EXISTS "${module}/run/rules-status.txt"
   OR NOT EXISTS "${module}/run/rules-status.json")
  message(FATAL_ERROR
    "Daemon first reconcile failed: ${daemon_first_output} ${daemon_first_error}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" status "${module}"
  RESULT_VARIABLE status_result
  OUTPUT_VARIABLE status_output
)
if(NOT status_result EQUAL 0 OR NOT status_output MATCHES "status: active")
  message(FATAL_ERROR "CLI did not expose daemon rules status")
endif()
execute_process(
  COMMAND "${PATHGUARDCTL}" reload "${module}"
  RESULT_VARIABLE reload_result
  OUTPUT_VARIABLE reload_output
)
if(NOT reload_result EQUAL 0 OR NOT reload_output MATCHES "reload requested")
  message(FATAL_ERROR "CLI reload request failed")
endif()

execute_process(
  COMMAND "${PATHGUARDD}" --module-dir "${module}" --compile
  RESULT_VARIABLE daemon_second_result
  OUTPUT_VARIABLE daemon_second_output
  ERROR_VARIABLE daemon_second_error
)
if(NOT daemon_second_result EQUAL 0
   OR NOT daemon_second_output MATCHES "unchanged=1")
  message(FATAL_ERROR
    "Daemon unchanged reconcile failed: ${daemon_second_output} ${daemon_second_error}")
endif()

file(READ "${valid}" comment_only)
string(PREPEND comment_only "# comment-only change\n")
file(WRITE "${module}/config/rules.toml" "${comment_only}")
execute_process(
  COMMAND "${PATHGUARDD}" --module-dir "${module}" --compile
  RESULT_VARIABLE comment_result
  OUTPUT_VARIABLE comment_output
  ERROR_VARIABLE comment_error
)
if(NOT comment_result EQUAL 0 OR NOT comment_output MATCHES "unchanged=1")
  message(FATAL_ERROR
    "Comment-only source change rewrote policy: ${comment_output} ${comment_error}")
endif()

file(COPY_FILE "${invalid}" "${module}/config/rules.toml")
execute_process(
  COMMAND "${PATHGUARDD}" --module-dir "${module}" --compile
  RESULT_VARIABLE invalid_daemon_result
  ERROR_VARIABLE invalid_daemon_error
)
if(invalid_daemon_result EQUAL 0
   OR NOT invalid_daemon_error MATCHES "previous policy remains active")
  message(FATAL_ERROR "Daemon did not retain policy after invalid source")
endif()
file(READ "${module}/run/rules-status.txt" status)
if(NOT status MATCHES "status: source_invalid"
   OR NOT status MATCHES "active_content_generation: [1-9]")
  message(FATAL_ERROR "Daemon source_invalid status is incomplete: ${status}")
endif()

file(REMOVE_RECURSE "${root}")
