if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "BINARY_DIR is required")
endif()

set(test_temp_root "${BINARY_DIR}/asset-guard-tmp")
file(MAKE_DIRECTORY "${test_temp_root}")

set(verifier "${SOURCE_DIR}/tests/baseline/validate_test_assets.cmake")
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${SOURCE_DIR}" -P "${verifier}"
  RESULT_VARIABLE real_result
  OUTPUT_VARIABLE real_output
  ERROR_VARIABLE real_error
)
if(NOT real_result EQUAL 0)
  message(FATAL_ERROR
    "RF0 test assets failed validation:\n${real_output}\n${real_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${SOURCE_DIR}"
    -DINJECT_BAD_SEED=ON
    -P "${verifier}"
  RESULT_VARIABLE bad_seed_result
)
if(bad_seed_result EQUAL 0)
  message(FATAL_ERROR "RF2 asset guard accepted an invalid fuzz seed hash")
endif()

set(empty_root "${test_temp_root}/rf0-empty-assets")
file(REMOVE_RECURSE "${empty_root}")
file(MAKE_DIRECTORY "${empty_root}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${SOURCE_DIR}"
    "-DASSET_ROOT=${empty_root}"
    -P "${verifier}"
  RESULT_VARIABLE missing_result
)
if(missing_result EQUAL 0)
  message(FATAL_ERROR "RF0 asset guard accepted missing provenance/license/manifest/seed")
endif()

set(bad_root "${test_temp_root}/rf0-bad-golden")
file(REMOVE_RECURSE "${bad_root}")
foreach(directory IN ITEMS
    "fixtures/toml-test-v2.2.0" "golden/rules" "integration/rules"
    "device/rules" "fuzz" "fuzz/seeds")
  file(MAKE_DIRECTORY "${bad_root}/${directory}")
endforeach()
foreach(relative IN ITEMS
    "fixtures/toml-test-v2.2.0/LICENSE"
    "fixtures/toml-test-v2.2.0/PROVENANCE.md"
    "fixtures/toml-test-v2.2.0/manifest.txt"
    "fixtures/toml-test-v2.2.0/files-toml-1.0.0"
    "fixtures/toml-test-v2.2.0/excluded-toml-1.1.txt"
    "golden/rules/README.md"
    "integration/rules/README.md"
    "device/rules/README.md"
    "fuzz/README.md"
    "fuzz/seeds/manifest.txt")
  file(COPY "${SOURCE_DIR}/tests/${relative}"
       DESTINATION "${bad_root}/${relative}/..")
endforeach()

file(COPY "${SOURCE_DIR}/tests/fixtures/toml-test-v2.2.0/valid"
     DESTINATION "${bad_root}/fixtures/toml-test-v2.2.0")
file(COPY "${SOURCE_DIR}/tests/fixtures/toml-test-v2.2.0/invalid"
     DESTINATION "${bad_root}/fixtures/toml-test-v2.2.0")
file(WRITE "${bad_root}/golden/rules/leak.golden"
  "diagnostic = internal { from = \"A\", to = \"B\" }")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${SOURCE_DIR}"
    "-DASSET_ROOT=${bad_root}"
    -P "${verifier}"
  RESULT_VARIABLE leak_result
)
if(leak_result EQUAL 0)
  message(FATAL_ERROR "RF0 asset guard accepted a generated TOML diagnostic leak")
endif()

set(non_member_root "${test_temp_root}/rf1-non-member-fixture")
file(REMOVE_RECURSE "${non_member_root}")
file(COPY "${SOURCE_DIR}/tests/fixtures/toml-test-v2.2.0/"
     DESTINATION "${non_member_root}")
file(MAKE_DIRECTORY "${non_member_root}/valid/inline-table")
file(WRITE "${non_member_root}/valid/inline-table/newline.toml"
  "a = {\n  b = 1,\n}\n")
file(SHA256 "${non_member_root}/valid/inline-table/newline.toml" non_member_sha)
file(APPEND "${non_member_root}/manifest.txt"
  "${non_member_sha}\tvalid/inline-table/newline.toml\ttoml-1.1-only\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXTURE_ROOT=${non_member_root}"
    -P "${SOURCE_DIR}/tests/baseline/validate_toml_test_fixtures.cmake"
  RESULT_VARIABLE non_member_result
)
if(non_member_result EQUAL 0)
  message(FATAL_ERROR "RF1 fixture guard accepted a TOML 1.1-only file")
endif()
