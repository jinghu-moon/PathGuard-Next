if(NOT DEFINED FIXTURE_ROOT)
  message(FATAL_ERROR "FIXTURE_ROOT is required")
endif()

set(version_file "${FIXTURE_ROOT}/PROVENANCE.md")
set(license_file "${FIXTURE_ROOT}/LICENSE")
set(allowed_file "${FIXTURE_ROOT}/files-toml-1.0.0")
set(excluded_file "${FIXTURE_ROOT}/excluded-toml-1.1.txt")
set(manifest_file "${FIXTURE_ROOT}/manifest.txt")
foreach(required IN ITEMS
    "${version_file}" "${license_file}" "${allowed_file}"
    "${excluded_file}" "${manifest_file}")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Required toml-test metadata is missing: ${required}")
  endif()
endforeach()

file(READ "${version_file}" provenance)
foreach(required_text IN ITEMS
    "toml-lang/toml-test" "v2.2.0" "TOML version：1.0"
    "files-toml-1.0.0" "MIT")
  string(FIND "${provenance}" "${required_text}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "toml-test provenance is incomplete: ${required_text}")
  endif()
endforeach()

file(STRINGS "${allowed_file}" allowed_paths)
file(STRINGS "${excluded_file}" excluded_lines)
foreach(line IN LISTS excluded_lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()
  list(FIND allowed_paths "${line}" allowed_index)
  if(NOT allowed_index EQUAL -1)
    message(FATAL_ERROR "TOML 1.1-only sentinel entered the 1.0 list: ${line}")
  endif()
endforeach()

file(STRINGS "${manifest_file}" manifest_lines)
set(manifest_paths)
set(manifest_count 0)
foreach(line IN LISTS manifest_lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()
  string(REPLACE "\t" ";" fields "${line}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 3)
    message(FATAL_ERROR "Invalid toml-test manifest row: ${line}")
  endif()
  list(GET fields 0 expected_sha256)
  list(GET fields 1 relative_path)
  list(GET fields 2 purpose)
  string(LENGTH "${expected_sha256}" sha256_length)
  if(NOT sha256_length EQUAL 64
     OR NOT expected_sha256 MATCHES "^[0-9a-f]+$"
     OR purpose STREQUAL "")
    message(FATAL_ERROR "Incomplete toml-test manifest row: ${line}")
  endif()
  list(FIND allowed_paths "${relative_path}" allowed_index)
  if(allowed_index EQUAL -1)
    message(FATAL_ERROR "Fixture is not in files-toml-1.0.0: ${relative_path}")
  endif()
  list(FIND manifest_paths "${relative_path}" duplicate_index)
  if(NOT duplicate_index EQUAL -1)
    message(FATAL_ERROR "Duplicate toml-test fixture: ${relative_path}")
  endif()
  list(APPEND manifest_paths "${relative_path}")
  set(fixture "${FIXTURE_ROOT}/${relative_path}")
  if(NOT EXISTS "${fixture}")
    message(FATAL_ERROR "Manifest fixture is missing: ${relative_path}")
  endif()
  file(SHA256 "${fixture}" actual_sha256)
  if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR "Fixture SHA-256 changed: ${relative_path}")
  endif()
  math(EXPR manifest_count "${manifest_count} + 1")
endforeach()

if(NOT manifest_count EQUAL 137)
  message(FATAL_ERROR "Expected 137 TOML 1.0 fixtures, found ${manifest_count}")
endif()

file(GLOB_RECURSE local_toml RELATIVE "${FIXTURE_ROOT}"
  "${FIXTURE_ROOT}/valid/*.toml" "${FIXTURE_ROOT}/invalid/*.toml")
list(SORT local_toml)
list(SORT manifest_paths)
if(NOT local_toml STREQUAL manifest_paths)
  message(FATAL_ERROR "Local TOML fixture set and manifest differ")
endif()

foreach(required_fixture IN ITEMS
    "valid/string/basic-escape-01.toml"
    "invalid/string/bad-escape-01.toml"
    "invalid/encoding/bad-utf8-in-string.toml"
    "valid/comment/everywhere.toml"
    "valid/array/array.toml"
    "invalid/array/missing-separator-01.toml"
    "valid/utf8-bom-01.toml"
    "valid/newline-crlf.toml")
  list(FIND manifest_paths "${required_fixture}" required_index)
  if(required_index EQUAL -1)
    message(FATAL_ERROR "Required TOML fixture category is absent: ${required_fixture}")
  endif()
endforeach()
