add_library(pathguard_core STATIC
  src/failure_policy.cpp
  src/export_worker.cpp
  src/path.cpp
  src/pattern.cpp
  src/pattern_runtime.cpp
  src/policy_v6.cpp
  src/provider_mapping.cpp
  src/provider_route_snapshot_registry.cpp
  src/route_provenance.cpp
  src/provenance_broker.cpp
  src/topology.cpp
  src/version.cpp
)

target_include_directories(pathguard_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(pathguard_core PUBLIC cxx_std_20)
