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
