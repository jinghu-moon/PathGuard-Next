if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(required_files
  "tests/fuzz/pattern_tokenizer_fuzzer.cpp"
  "tests/fuzz/pattern_matcher_fuzzer.cpp"
  "tests/fuzz/pattern_fuzz_smoke.cpp"
  "tests/fuzz/pattern_corpus.h"
  "tests/fuzz/seeds/pattern-v1/manifest.txt"
  "tests/perf/pattern_benchmark.cpp"
  "tests/unit/pattern_property_smoke_test.cpp"
  "core/include/pathguard/pattern_limits.h"
)

set(failures)
foreach(relative_path IN LISTS required_files)
  if(NOT EXISTS "${SOURCE_DIR}/${relative_path}")
    list(APPEND failures "missing harness file: ${relative_path}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/tests/CMakeLists.txt" tests_cmake)
foreach(required_target IN ITEMS
    pathguard_pattern_tokenizer_fuzzer
    pathguard_pattern_matcher_fuzzer
    pathguard_pattern_fuzz_smoke
    pathguard_pattern_property_smoke
    pathguard_pattern_benchmark)
  if(NOT tests_cmake MATCHES "${required_target}")
    list(APPEND failures "missing CMake target: ${required_target}")
  endif()
endforeach()

set(benchmark_path "${SOURCE_DIR}/tests/perf/pattern_benchmark.cpp")
if(EXISTS "${benchmark_path}")
  file(READ "${benchmark_path}" benchmark_source)
  foreach(required_contract IN ITEMS
      schema_version
      zero_candidate
      one_candidate
      multi_candidate)
    if(NOT benchmark_source MATCHES "${required_contract}")
      list(APPEND failures
        "benchmark contract is missing: ${required_contract}")
    endif()
  endforeach()
endif()

set(manifest_path
  "${SOURCE_DIR}/tests/fuzz/seeds/pattern-v1/manifest.txt")
if(EXISTS "${manifest_path}")
  file(STRINGS "${manifest_path}" manifest_records
    REGEX "^(tokenizer|matcher)\t")
  foreach(record IN LISTS manifest_records)
    string(REPLACE "\t" ";" fields "${record}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 4)
      list(APPEND failures "invalid Pattern corpus record: ${record}")
      continue()
    endif()
    list(GET fields 1 seed_name)
    list(GET fields 2 expected_sha256)
    set(seed_path
      "${SOURCE_DIR}/tests/fuzz/seeds/pattern-v1/${seed_name}")
    if(NOT EXISTS "${seed_path}")
      list(APPEND failures "missing Pattern corpus seed: ${seed_name}")
      continue()
    endif()
    file(SHA256 "${seed_path}" actual_sha256)
    if(NOT actual_sha256 STREQUAL expected_sha256)
      list(APPEND failures "Pattern corpus SHA mismatch: ${seed_name}")
    endif()
  endforeach()
  list(LENGTH manifest_records manifest_record_count)
  if(NOT manifest_record_count EQUAL 2)
    list(APPEND failures "Pattern corpus must contain tokenizer and matcher seeds")
  endif()
endif()

set(shared_consumers
  "tests/fuzz/pattern_fuzz_smoke.cpp"
  "tests/unit/pattern_harness_contract_test.cpp"
  "tests/unit/pattern_property_smoke_test.cpp"
  "tests/perf/pattern_benchmark.cpp")
foreach(relative_path IN LISTS shared_consumers)
  if(EXISTS "${SOURCE_DIR}/${relative_path}")
    file(READ "${SOURCE_DIR}/${relative_path}" consumer_source)
    if(NOT consumer_source MATCHES "pattern_corpus.h")
      list(APPEND failures "shared corpus loader missing from: ${relative_path}")
    endif()
    if(NOT consumer_source MATCHES "pattern_limits.h")
      list(APPEND failures "production limits missing from: ${relative_path}")
    endif()
  endif()
endforeach()

file(GLOB_RECURSE core_sources
  "${SOURCE_DIR}/core/include/*"
  "${SOURCE_DIR}/core/src/*")
set(limits_definition_count 0)
foreach(core_source IN LISTS core_sources)
  file(READ "${core_source}" core_text)
  if(core_text MATCHES
      "inline constexpr PatternLimitsProfile kPatternLimitsProfileV1")
    math(EXPR limits_definition_count "${limits_definition_count} + 1")
  endif()
endforeach()
if(NOT limits_definition_count EQUAL 1)
  list(APPEND failures
    "PatternLimitsProfile production definition count is ${limits_definition_count}, expected 1")
endif()

set(fuzz_readme "${SOURCE_DIR}/tests/fuzz/README.md")
if(EXISTS "${fuzz_readme}")
  file(READ "${fuzz_readme}" fuzz_documentation)
  foreach(required_documentation IN ITEMS
      pathguard_pattern_tokenizer_fuzzer
      pathguard_pattern_matcher_fuzzer
      "-fsanitize=address,undefined"
      "--format=jsonl"
      "--format=tsv")
    if(NOT fuzz_documentation MATCHES "${required_documentation}")
      list(APPEND failures
        "missing Pattern harness documentation: ${required_documentation}")
    endif()
  endforeach()
endif()

if(failures)
  string(JOIN "\n  - " failure_text ${failures})
  message(FATAL_ERROR
    "Pattern benchmark/fuzz harness is incomplete:\n  - ${failure_text}")
endif()
