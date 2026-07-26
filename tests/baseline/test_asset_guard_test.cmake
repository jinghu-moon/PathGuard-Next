if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

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

set(empty_root "${CMAKE_CURRENT_BINARY_DIR}/rf0-empty-assets")
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

set(bad_root "${CMAKE_CURRENT_BINARY_DIR}/rf0-bad-golden")
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
    "golden/rules/README.md"
    "integration/rules/README.md"
    "device/rules/README.md"
    "fuzz/README.md"
    "fuzz/seeds/manifest.txt")
  file(COPY "${SOURCE_DIR}/tests/${relative}"
       DESTINATION "${bad_root}/${relative}/..")
endforeach()
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
