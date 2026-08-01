if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(production_roots
  "${SOURCE_DIR}/core"
  "${SOURCE_DIR}/rules"
  "${SOURCE_DIR}/daemon"
  "${SOURCE_DIR}/cli"
  "${SOURCE_DIR}/zygisk"
  "${SOURCE_DIR}/native"
  "${SOURCE_DIR}/module"
  "${SOURCE_DIR}/protocol"
)

set(production_files)
foreach(root IN LISTS production_roots)
  file(GLOB_RECURSE root_files
    "${root}/*.cpp"
    "${root}/*.h"
    "${root}/*.hpp"
    "${root}/*.mk"
    "${root}/CMakeLists.txt"
    "${root}/*.toml"
    "${root}/*.md")
  list(APPEND production_files ${root_files})
endforeach()

set(forbidden_patterns
  "ProviderCompat"
  "provider_compat"
  "file_picker"
  "RestoreAbsolutePath"
  "provider_path_mapper"
  "PolicyDocument"
  "EncodePolicy\\("
  "DecodePolicy\\("
  "ParseRulesDocument\\(")

foreach(path IN LISTS production_files)
  file(READ "${path}" content)
  foreach(pattern IN LISTS forbidden_patterns)
    if(content MATCHES "${pattern}")
      file(RELATIVE_PATH relative "${SOURCE_DIR}" "${path}")
      message(FATAL_ERROR
        "legacy production contract '${pattern}' remains in ${relative}")
    endif()
  endforeach()
endforeach()

file(READ "${SOURCE_DIR}/core/include/pathguard/policy_format.h" policy_format)
if(NOT policy_format MATCHES "kFormatVersion = 6")
  message(FATAL_ERROR "policy format 6 is not the sole production version")
endif()
