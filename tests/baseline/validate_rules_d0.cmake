if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(decision "${SOURCE_DIR}/docs/decisions/rules-compiler-d0.md")
set(cpp_result "${SOURCE_DIR}/tests/d0/results/cpp-tomlplusplus.txt")
set(rust_result "${SOURCE_DIR}/tests/d0/results/rust-toml-edit.txt")
set(strict_result "${SOURCE_DIR}/tests/d0/results/strict-toml.txt")
set(syntax_options "${SOURCE_DIR}/tests/golden/rules/d0-syntax-options.tsv")
set(binder "${SOURCE_DIR}/tests/golden/rules/d0/binder-neutral.tsv")
set(parse_errors "${SOURCE_DIR}/tests/golden/rules/d0/parse-error-map.tsv")
set(binding_cases "${SOURCE_DIR}/tests/golden/rules/d0/binding-cases.tsv")
if(DEFINED INJECT_MISSING_CANDIDATE AND INJECT_MISSING_CANDIDATE)
  set(rust_result "${SOURCE_DIR}/tests/d0/results/intentionally-missing.txt")
endif()
foreach(required IN ITEMS
    "${decision}" "${cpp_result}" "${rust_result}" "${strict_result}"
    "${syntax_options}" "${binder}" "${parse_errors}" "${binding_cases}"
    "${SOURCE_DIR}/rules/include/pathguard/rules_contract.h"
    "${SOURCE_DIR}/third_party/tomlplusplus/toml.hpp"
    "${SOURCE_DIR}/third_party/tomlplusplus/LICENSE")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Required RF1 D0 evidence is missing: ${required}")
  endif()
endforeach()

file(READ "${decision}" decision_text)
if(DEFINED INJECT_BAD_DECISION AND INJECT_BAD_DECISION)
  string(REPLACE "kill criterion" "removed criterion" decision_text "${decision_text}")
endif()
foreach(required_text IN ITEMS
    "decision: cpp-tomlplusplus-arrow"
    "kill criterion"
    "严格 TOML"
    "删除清单"
    "RulesLimits"
    "完整命令"
    "原始测量")
  string(FIND "${decision_text}" "${required_text}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "D0 decision is incomplete: ${required_text}")
  endif()
endforeach()

file(READ "${cpp_result}" cpp_text)
foreach(required_text IN ITEMS
    "binder_ascii_unicode_bom_crlf_crossline_multiple=pass"
    "hidden_marker=not-required"
    "strict_inline_policy_v5=207-bytes-pass"
    "android_stripped_control_plane_bytes="
    "host_4096_peak_working_set_bytes=")
  string(FIND "${cpp_text}" "${required_text}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "C++ D0 result is incomplete: ${required_text}")
  endif()
endforeach()

file(READ "${rust_result}" rust_text)
foreach(required_text IN ITEMS
    "toml_parser=1.0.4"
    "document_mut_clears_spans=pass"
    "c_abi_unwind_panic=PG-COMPILER-INTERNAL-pass"
    "selection=not-selected")
  string(FIND "${rust_text}" "${required_text}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Rust D0 result is incomplete: ${required_text}")
  endif()
endforeach()

file(READ "${strict_result}" strict_text)
foreach(required_text IN ITEMS
    "strict_inline_policy_v5=207-bytes-pass"
    "strict_mapping_policy_v5=207-bytes-pass"
    "coexist_with_arrow_in_format1=no")
  string(FIND "${strict_text}" "${required_text}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Strict TOML comparison is incomplete: ${required_text}")
  endif()
endforeach()

file(READ "${syntax_options}" syntax_text)
foreach(required_text IN ITEMS "arrow|" "inline-table|" "mapping-subtable|")
  string(FIND "${syntax_text}" "${required_text}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "D0 syntax comparison is incomplete: ${required_text}")
  endif()
endforeach()

file(SHA256 "${SOURCE_DIR}/third_party/tomlplusplus/toml.hpp" toml_sha256)
if(NOT toml_sha256 STREQUAL
   "2089217190195e12e9a4a454bc94cfb95b58a07ff927f1505d068188c2f864df")
  message(FATAL_ERROR "Vendored toml++ v3.4.0 hash changed")
endif()

if(EXISTS "${SOURCE_DIR}/rules-compiler/Cargo.toml"
   OR (DEFINED INJECT_RUST_ARTIFACT AND INJECT_RUST_ARTIFACT))
  message(FATAL_ERROR "Unselected Rust D0 artifact was not deleted")
endif()

file(READ "${SOURCE_DIR}/tests/d0/cpp_toml_adapter_spike.cpp" adapter_source)
string(FIND "${adapter_source}" "__pg_internal_arrow" hidden_marker)
if(NOT hidden_marker EQUAL -1)
  message(FATAL_ERROR "Selected C++ adapter retained hidden-marker fallback")
endif()
