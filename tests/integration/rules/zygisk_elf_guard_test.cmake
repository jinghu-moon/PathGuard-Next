if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(verifier "${SOURCE_DIR}/scripts/verify-zygisk-elf.cmake")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -DTEST_TEXT=mount_executor -P "${verifier}"
  RESULT_VARIABLE clean_result
)
if(NOT clean_result EQUAL 0)
  message(FATAL_ERROR "ELF guard rejected clean symbol text")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DTEST_TEXT=pathguard_rules_compiler_ParseRulesDocument
    -P "${verifier}"
  RESULT_VARIABLE injected_result
)
if(injected_result EQUAL 0)
  message(FATAL_ERROR "ELF guard accepted injected compiler symbol")
endif()
