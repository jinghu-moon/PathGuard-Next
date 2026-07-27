if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(DEFINED ASSET_ROOT)
  set(root "${ASSET_ROOT}")
else()
  set(root "${SOURCE_DIR}/tests")
endif()

set(required_files
  "${root}/fixtures/toml-test-v2.2.0/LICENSE"
  "${root}/fixtures/toml-test-v2.2.0/PROVENANCE.md"
  "${root}/fixtures/toml-test-v2.2.0/manifest.txt"
  "${root}/fixtures/toml-test-v2.2.0/files-toml-1.0.0"
  "${root}/fixtures/toml-test-v2.2.0/excluded-toml-1.1.txt"
  "${root}/golden/rules/README.md"
  "${root}/integration/rules/README.md"
  "${root}/device/rules/README.md"
  "${root}/fuzz/README.md"
  "${root}/fuzz/seeds/manifest.txt"
)
foreach(required IN LISTS required_files)
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Required RF0 test asset is missing: ${required}")
  endif()
endforeach()

set(FIXTURE_ROOT "${root}/fixtures/toml-test-v2.2.0")
include("${SOURCE_DIR}/tests/baseline/validate_toml_test_fixtures.cmake")

file(READ "${root}/fixtures/toml-test-v2.2.0/PROVENANCE.md" provenance)
foreach(required_text IN ITEMS "v2.2.0" "TOML version：1.0" "files-toml-1.0.0")
  string(FIND "${provenance}" "${required_text}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Fixture provenance is incomplete: ${required_text}")
  endif()
endforeach()

file(GLOB_RECURSE golden_files
  "${root}/golden/rules/*.golden"
  "${root}/golden/rules/*.json"
  "${root}/golden/rules/*.txt"
)
foreach(golden IN LISTS golden_files)
  file(READ "${golden}" golden_text)
  string(FIND "${golden_text}" "{ from, to }" forbidden_short)
  string(FIND "${golden_text}" "{ from =" forbidden_generated)
  if(NOT forbidden_short EQUAL -1 OR NOT forbidden_generated EQUAL -1)
    message(FATAL_ERROR
      "User-facing golden exposes internal desugared TOML: ${golden}")
  endif()
endforeach()

set(seed_root "${root}/fuzz/seeds")
file(STRINGS "${seed_root}/manifest.txt" seed_lines)
set(seed_count 0)
set(manifest_seed_names)
foreach(line IN LISTS seed_lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()
  string(REPLACE "\t" ";" fields "${line}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 4)
    message(FATAL_ERROR "Invalid fuzz seed manifest row: ${line}")
  endif()
  list(GET fields 0 target)
  list(GET fields 1 seed_name)
  list(GET fields 2 expected_sha256)
  list(GET fields 3 purpose)
  list(APPEND manifest_seed_names "${seed_name}")
  string(SUBSTRING "${expected_sha256}" 0 16 hash_prefix)
  if(NOT seed_name MATCHES "^${target}-${hash_prefix}\\.seed$"
     OR purpose STREQUAL "")
    message(FATAL_ERROR "Invalid fuzz seed identity: ${seed_name}")
  endif()
  set(seed_file "${seed_root}/${seed_name}")
  if(NOT EXISTS "${seed_file}")
    message(FATAL_ERROR "Fuzz seed is missing: ${seed_name}")
  endif()
  file(SHA256 "${seed_file}" actual_sha256)
  if(DEFINED INJECT_BAD_SEED AND INJECT_BAD_SEED)
    set(actual_sha256 "injected-invalid-sha256")
  endif()
  if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR "Fuzz seed SHA-256 changed: ${seed_name}")
  endif()
  math(EXPR seed_count "${seed_count} + 1")
endforeach()
if(NOT seed_count EQUAL 3)
  message(FATAL_ERROR "Expected 3 RF2/RF3 fuzz seeds, found ${seed_count}")
endif()
file(GLOB local_seed_files RELATIVE "${seed_root}" "${seed_root}/*")
list(REMOVE_ITEM local_seed_files "manifest.txt")
list(SORT local_seed_files)
list(SORT manifest_seed_names)
if(NOT local_seed_files STREQUAL manifest_seed_names)
  message(FATAL_ERROR "Local fuzz seed set and manifest differ")
endif()
