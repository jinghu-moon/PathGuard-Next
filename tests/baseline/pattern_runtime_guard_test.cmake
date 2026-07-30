if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(required_files
  "core/include/pathguard/pattern_runtime.h"
  "core/src/pattern_runtime.cpp"
  "rules/include/pathguard/rules/selector_builder.h"
  "rules/src/selector_builder.cpp"
  "tests/unit/selector_builder_test.cpp"
  "tests/unit/candidate_index_test.cpp"
  "tests/unit/action_evaluator_test.cpp"
  "tests/unit/operation_plan_test.cpp")
set(failures)
foreach(relative_path IN LISTS required_files)
  if(NOT EXISTS "${SOURCE_DIR}/${relative_path}")
    list(APPEND failures "missing runtime source: ${relative_path}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/tests/CMakeLists.txt" tests_cmake)
foreach(target IN ITEMS
    pathguard_selector_builder_test
    pathguard_candidate_index_test
    pathguard_action_evaluator_test
    pathguard_operation_plan_test)
  if(NOT tests_cmake MATCHES "${target}")
    list(APPEND failures "missing runtime CTest target: ${target}")
  endif()
endforeach()

if(EXISTS "${SOURCE_DIR}/core/include/pathguard/pattern_runtime.h")
  file(READ "${SOURCE_DIR}/core/include/pathguard/pattern_runtime.h" runtime_header)
  foreach(contract IN ITEMS
      "IdentityKey" "CandidateIndex" "matcher_invocations"
      "ActionEvaluator" "DecisionReason" "OperationPlan")
    if(NOT runtime_header MATCHES "${contract}")
      list(APPEND failures "runtime header misses contract: ${contract}")
    endif()
  endforeach()
endif()

if(failures)
  string(JOIN "\n  - " failure_text ${failures})
  message(FATAL_ERROR "Pattern runtime contract is incomplete:\n  - ${failure_text}")
endif()
