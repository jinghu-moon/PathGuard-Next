if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(required_files
  "rules/include/pathguard/rules/schema_v2.h"
  "core/include/pathguard/pattern.h"
  "rules/src/schema_v2.cpp"
  "core/src/pattern.cpp"
  "tests/unit/rules_schema_v2_test.cpp"
  "tests/unit/rules_pattern_test.cpp"
  "tests/unit/rules_brace_test.cpp")
set(failures)
foreach(relative_path IN LISTS required_files)
  if(NOT EXISTS "${SOURCE_DIR}/${relative_path}")
    list(APPEND failures "missing P0 source: ${relative_path}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/tests/CMakeLists.txt" tests_cmake)
foreach(target IN ITEMS
    pathguard_rules_schema_v2_test
    pathguard_rules_pattern_test
    pathguard_rules_brace_test)
  if(NOT tests_cmake MATCHES "${target}")
    list(APPEND failures "missing P0 CTest target: ${target}")
  endif()
endforeach()

if(EXISTS "${SOURCE_DIR}/tests/unit/rules_schema_v2_test.cpp")
  file(READ "${SOURCE_DIR}/tests/unit/rules_schema_v2_test.cpp" schema_test)
  foreach(contract IN ITEMS
      "format = 2" "format = 1" "redirect_rules" "deny_rules"
      "provider" "priority" "preserve" "collision" "enforcement")
    if(NOT schema_test MATCHES "${contract}")
      list(APPEND failures "schema test misses contract: ${contract}")
    endif()
  endforeach()
endif()

if(EXISTS "${SOURCE_DIR}/tests/unit/rules_pattern_test.cpp")
  file(READ "${SOURCE_DIR}/tests/unit/rules_pattern_test.cpp" pattern_test)
  foreach(contract IN ITEMS
      "STAR_COMPONENT" "ONE_COMPONENT_CHAR" "GLOBSTAR_COMPONENT"
      "CHAR_CLASS" "InvalidPathEncoding" "BudgetExceeded")
    if(NOT pattern_test MATCHES "${contract}")
      list(APPEND failures "pattern test misses contract: ${contract}")
    endif()
  endforeach()
endif()

if(EXISTS "${SOURCE_DIR}/tests/unit/rules_brace_test.cpp")
  file(READ "${SOURCE_DIR}/tests/unit/rules_brace_test.cpp" brace_test)
  foreach(contract IN ITEMS "32" "64.*1024" "nested" "atomic")
    if(NOT brace_test MATCHES "${contract}")
      list(APPEND failures "brace test misses contract: ${contract}")
    endif()
  endforeach()
endif()

if(failures)
  string(JOIN "\n  - " failure_text ${failures})
  message(FATAL_ERROR "Pattern core P0 contract is incomplete:\n  - ${failure_text}")
endif()
