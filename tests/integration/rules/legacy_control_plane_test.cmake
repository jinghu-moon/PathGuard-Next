if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR
   OR NOT DEFINED PATHGUARDCTL OR NOT DEFINED PATHGUARDD)
  message(FATAL_ERROR
    "SOURCE_DIR, BINARY_DIR, PATHGUARDCTL and PATHGUARDD are required")
endif()

set(root "${BINARY_DIR}/rf0-legacy-control-plane")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}")
set(valid "${SOURCE_DIR}/tests/fixtures/legacy-rules/valid-redirect.ini")
set(invalid "${SOURCE_DIR}/tests/fixtures/legacy-rules/invalid-schema.ini")

execute_process(
  COMMAND "${PATHGUARDCTL}"
  RESULT_VARIABLE usage_result
  OUTPUT_VARIABLE usage_output
  ERROR_VARIABLE usage_error
)
if(NOT usage_result EQUAL 2 OR NOT usage_error MATCHES "rules.ini")
  message(FATAL_ERROR "CLI legacy usage contract changed: ${usage_error}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" validate "${valid}"
  RESULT_VARIABLE validate_result
  OUTPUT_VARIABLE validate_output
  ERROR_VARIABLE validate_error
)
if(NOT validate_result EQUAL 0 OR NOT validate_output MATCHES "valid: 1 package")
  message(FATAL_ERROR
    "CLI valid characterization failed: ${validate_output} ${validate_error}")
endif()

set(cli_policy "${root}/cli-policy.bin")
execute_process(
  COMMAND "${PATHGUARDCTL}" compile "${valid}" "${cli_policy}"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_output
  ERROR_VARIABLE compile_error
)
if(NOT compile_result EQUAL 0 OR NOT EXISTS "${cli_policy}")
  message(FATAL_ERROR
    "CLI compile characterization failed: ${compile_output} ${compile_error}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" validate "${invalid}"
  RESULT_VARIABLE invalid_result
  OUTPUT_VARIABLE invalid_output
  ERROR_VARIABLE invalid_error
)
if(invalid_result EQUAL 0 OR NOT invalid_error MATCHES "line 1:")
  message(FATAL_ERROR "CLI invalid characterization failed: ${invalid_error}")
endif()

set(module "${root}/module")
file(MAKE_DIRECTORY "${module}/config" "${module}/run")
file(COPY_FILE "${valid}" "${module}/config/rules.ini")
execute_process(
  COMMAND "${PATHGUARDD}" --module-dir "${module}" --compile
  RESULT_VARIABLE daemon_first_result
  OUTPUT_VARIABLE daemon_first_output
  ERROR_VARIABLE daemon_first_error
)
if(NOT daemon_first_result EQUAL 0
   OR NOT daemon_first_output MATCHES "published=1"
   OR NOT EXISTS "${module}/run/policy.bin")
  message(FATAL_ERROR
    "Daemon first compile failed: ${daemon_first_output} ${daemon_first_error}")
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
    "Daemon unchanged characterization failed: ${daemon_second_output} ${daemon_second_error}")
endif()

file(READ "${valid}" changed_rules)
string(REPLACE "PathGuard/Target" "PathGuard/Changed" changed_rules "${changed_rules}")
file(WRITE "${module}/config/rules.ini" "${changed_rules}")
file(MAKE_DIRECTORY "${module}/run/policy.bin.tmp")
execute_process(
  COMMAND "${PATHGUARDD}" --module-dir "${module}" --compile
  RESULT_VARIABLE temp_failure_result
  OUTPUT_VARIABLE temp_failure_output
  ERROR_VARIABLE temp_failure_error
)
if(temp_failure_result EQUAL 0
   OR NOT temp_failure_error MATCHES "cannot create policy.bin.tmp")
  message(FATAL_ERROR
    "Daemon fixed temp-path failure was not preserved: ${temp_failure_error}")
endif()

set(replace_module "${root}/replace-module")
file(MAKE_DIRECTORY
  "${replace_module}/config"
  "${replace_module}/run"
  "${replace_module}/run/policy.bin")
file(COPY_FILE "${valid}" "${replace_module}/config/rules.ini")
execute_process(
  COMMAND "${PATHGUARDD}" --module-dir "${replace_module}" --compile
  RESULT_VARIABLE replace_failure_result
  OUTPUT_VARIABLE replace_failure_output
  ERROR_VARIABLE replace_failure_error
)
if(replace_failure_result EQUAL 0
   OR NOT replace_failure_error MATCHES "cannot atomically publish policy.bin")
  message(FATAL_ERROR
    "Daemon replace failure was not preserved: ${replace_failure_error}")
endif()

file(REMOVE_RECURSE "${root}")
