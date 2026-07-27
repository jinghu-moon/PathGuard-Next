if(NOT DEFINED BINARY_DIR OR NOT DEFINED PATHGUARDCTL)
  message(FATAL_ERROR "BINARY_DIR and PATHGUARDCTL are required")
endif()
set(root "${BINARY_DIR}/rf9-tools")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}")
set(old "${root}/old.toml")
set(new "${root}/new.toml")
file(WRITE "${old}"
  "format = 1\n[compatibility]\nallow_legacy_mount=true\n"
  "[apps.\"com.example.app\"]\n"
  "redirect=[\"A\" -> \"B\",\"A\" -> \"B\",\"C\" -> \"D\"]\n")
file(WRITE "${new}"
  "format = 1\n[apps.\"com.example.app\"]\n"
  "redirect=[\"A\" -> \"X\",\"E\" -> \"F\"]\n")

execute_process(COMMAND "${PATHGUARDCTL}" lint "${old}"
  RESULT_VARIABLE lint_result OUTPUT_VARIABLE lint_output
  ERROR_VARIABLE lint_error)
if(NOT lint_result EQUAL 0
   OR NOT lint_output MATCHES "PG-RULE-REDUNDANT"
   OR NOT lint_output MATCHES "PG-LINT-LEGACY")
  message(FATAL_ERROR "lint output mismatch: ${lint_output}${lint_error}")
endif()

execute_process(COMMAND "${PATHGUARDCTL}" plan "${old}" "${new}"
  RESULT_VARIABLE plan_result OUTPUT_VARIABLE plan_output
  ERROR_VARIABLE plan_error)
if(NOT plan_result EQUAL 0
   OR NOT plan_output MATCHES "modify com.example.app redirect A from=B to=X"
   OR NOT plan_output MATCHES "remove com.example.app redirect C from=D"
   OR NOT plan_output MATCHES "add com.example.app redirect E to=F")
  message(FATAL_ERROR "plan output mismatch: ${plan_output}${plan_error}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" explain --path "${new}"
    com.example.app A/file
  RESULT_VARIABLE explain_result OUTPUT_VARIABLE explain_output
  ERROR_VARIABLE explain_error)
if(NOT explain_result EQUAL 0
   OR NOT explain_output MATCHES "match=redirect A -> X"
   OR NOT explain_output MATCHES "shadowed_parent=none")
  message(FATAL_ERROR "explain output mismatch: ${explain_output}${explain_error}")
endif()

file(GLOB unexpected_policy "${root}/*.bin" "${root}/run/*")
if(unexpected_policy)
  message(FATAL_ERROR "RF9 tools wrote policy files: ${unexpected_policy}")
endif()
file(REMOVE_RECURSE "${root}")
