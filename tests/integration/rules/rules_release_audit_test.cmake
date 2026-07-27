if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(production_roots core rules daemon cli zygisk native module protocol)
set(production_files)
foreach(root IN LISTS production_roots)
  file(GLOB_RECURSE found
    "${SOURCE_DIR}/${root}/*.h" "${SOURCE_DIR}/${root}/*.hpp"
    "${SOURCE_DIR}/${root}/*.c" "${SOURCE_DIR}/${root}/*.cc"
    "${SOURCE_DIR}/${root}/*.cpp" "${SOURCE_DIR}/${root}/*.cmake"
    "${SOURCE_DIR}/${root}/*.txt" "${SOURCE_DIR}/${root}/*.mk"
    "${SOURCE_DIR}/${root}/*.ps1" "${SOURCE_DIR}/${root}/*.sh"
    "${SOURCE_DIR}/${root}/*.toml" "${SOURCE_DIR}/${root}/*.md")
  list(APPEND production_files ${found})
endforeach()

set(forbidden
  "ParseRulesIni" "SplitArrow" "rules\\.ini" "legacy_rules_control"
  "policy\\.bin\\.tmp" "__pg_arrow" "toml_edit" "Cargo\\.toml")
if(DEFINED INJECT_FORBIDDEN)
  list(APPEND forbidden "pathguard_rules_compiler")
endif()
foreach(path IN LISTS production_files)
  file(READ "${path}" content)
  foreach(pattern IN LISTS forbidden)
    if(content MATCHES "${pattern}")
      message(FATAL_ERROR "forbidden production artifact '${pattern}' in ${path}")
    endif()
  endforeach()
endforeach()

set(toml_header "${SOURCE_DIR}/third_party/tomlplusplus/toml.hpp")
set(toml_license "${SOURCE_DIR}/third_party/tomlplusplus/LICENSE")
set(toml_record "${SOURCE_DIR}/third_party/tomlplusplus/README.md")
foreach(path IN ITEMS "${toml_header}" "${toml_license}" "${toml_record}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "toml++ dependency record missing: ${path}")
  endif()
endforeach()
file(SHA256 "${toml_header}" actual_hash)
if(NOT actual_hash STREQUAL
   "2089217190195e12e9a4a454bc94cfb95b58a07ff927f1505d068188c2f864df")
  message(FATAL_ERROR "toml++ hash changed without dependency review: ${actual_hash}")
endif()
file(READ "${toml_header}" toml_content)
foreach(version IN ITEMS
    "#define TOML_LIB_MAJOR 3"
    "#define TOML_LIB_MINOR 4"
    "#define TOML_LIB_PATCH 0")
  string(FIND "${toml_content}" "${version}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "toml++ version lock missing: ${version}")
  endif()
endforeach()

set(fixture_root "${SOURCE_DIR}/tests/fixtures/toml-test-v2.2.0")
foreach(name IN ITEMS LICENSE PROVENANCE.md manifest.txt files-toml-1.0.0)
  if(NOT EXISTS "${fixture_root}/${name}")
    message(FATAL_ERROR "toml-test provenance artifact missing: ${name}")
  endif()
endforeach()
message(STATUS "RF8 release audit passed: one parser, one source format, locked licenses")
