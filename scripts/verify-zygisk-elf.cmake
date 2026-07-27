set(scan_text "")
if(DEFINED TEST_TEXT)
  set(scan_text "${TEST_TEXT}")
elseif(DEFINED ELF AND DEFINED NM AND DEFINED STRINGS)
  if(NOT EXISTS "${ELF}")
    message(FATAL_ERROR "Zygisk ELF does not exist: ${ELF}")
  endif()
  execute_process(
    COMMAND "${NM}" -a "${ELF}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE nm_output
    ERROR_VARIABLE nm_error
  )
  if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "llvm-nm failed: ${nm_error}")
  endif()
  execute_process(
    COMMAND "${NM}" -D "${ELF}"
    RESULT_VARIABLE dynamic_result
    OUTPUT_VARIABLE dynamic_output
    ERROR_VARIABLE dynamic_error
  )
  if(NOT dynamic_result EQUAL 0)
    message(FATAL_ERROR "llvm-nm -D failed: ${dynamic_error}")
  endif()
  execute_process(
    COMMAND "${STRINGS}" "${ELF}"
    RESULT_VARIABLE strings_result
    OUTPUT_VARIABLE strings_output
    ERROR_VARIABLE strings_error
  )
  if(NOT strings_result EQUAL 0)
    message(FATAL_ERROR "llvm-strings failed: ${strings_error}")
  endif()
  string(APPEND scan_text "${nm_output}\n${dynamic_output}\n${strings_output}")
  if(DEFINED LINK_MAP AND EXISTS "${LINK_MAP}")
    file(READ "${LINK_MAP}" link_map_text)
    string(APPEND scan_text "\n${link_map_text}")
  elseif(DEFINED LINK_MAP)
    message(FATAL_ERROR "Zygisk link map does not exist: ${LINK_MAP}")
  endif()
else()
  message(FATAL_ERROR "provide TEST_TEXT or ELF/NM/STRINGS")
endif()

set(forbidden
  "toml::|toml\\+\\+|toml_edit|pathguard_rules|pathguard::rules|CompileRules|ParseRulesDocument|RenderDiagnostic|RulesBuildResult|pg_rules_|rust_eh_personality|__rust")
if(scan_text MATCHES "${forbidden}")
  message(FATAL_ERROR "Zygisk ELF/link map contains parser/compiler/runtime evidence")
endif()
