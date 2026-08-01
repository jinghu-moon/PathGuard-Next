if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(production_files
  "${SOURCE_DIR}/core/CMakeLists.txt"
  "${SOURCE_DIR}/core/include/pathguard/policy_v6.h"
  "${SOURCE_DIR}/daemon/CMakeLists.txt"
  "${SOURCE_DIR}/daemon/src/main.cpp"
  "${SOURCE_DIR}/daemon/src/rules_control.cpp"
  "${SOURCE_DIR}/cli/src/main.cpp"
  "${SOURCE_DIR}/native/Android.mk"
  "${SOURCE_DIR}/module/action.sh"
  "${SOURCE_DIR}/module/post-fs-data.sh"
  "${SOURCE_DIR}/module/service.sh")
set(all_text "")
foreach(path IN LISTS production_files)
  file(READ "${path}" text)
  string(APPEND all_text "\n${text}")
endforeach()
if(DEFINED INJECT_OLD_FORMAT AND INJECT_OLD_FORMAT)
  string(APPEND all_text " ParseRulesIni rules.ini policy.bin.tmp SplitArrow")
endif()
if(all_text MATCHES "ParseRulesIni|rules\\.ini|policy\\.bin\\.tmp|SplitArrow")
  message(FATAL_ERROR "production tree still references the legacy rule format")
endif()
if(NOT EXISTS "${SOURCE_DIR}/module/config/rules.toml")
  message(FATAL_ERROR "module template is missing rules.toml")
endif()
if(EXISTS "${SOURCE_DIR}/module/config/rules.ini")
  message(FATAL_ERROR "module template still contains rules.ini")
endif()
file(GLOB module_configs "${SOURCE_DIR}/module/config/*")
list(LENGTH module_configs config_count)
if(NOT config_count EQUAL 1)
  message(FATAL_ERROR "module must package exactly one configuration source")
endif()
