if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
file(READ "${SOURCE_DIR}/zygisk/src/provider_redirect_hook.cpp" hook)
file(READ "${SOURCE_DIR}/zygisk/include/pathguard/policy_snapshot_domain.hpp" snapshot)
file(READ "${SOURCE_DIR}/native/Android.mk" android_mk)
file(READ "${SOURCE_DIR}/zygisk/src/module_entry.cpp" entry)
file(READ "${SOURCE_DIR}/daemon/src/main.cpp" daemon)
foreach(contract IN ITEMS
    "PolicySnapshotDomain"
    "snapshot_guard"
    "ResolveStoragePathParent"
    "BuildPinnedProcPath")
  if(NOT hook MATCHES "${contract}")
    message(FATAL_ERROR "production hook misses ${contract}")
  endif()
endforeach()
string(FIND "${hook}" "PlanCanonicalPath(rewrite)" canonical_plan_at)
string(FIND "${hook}" "g_realpath(pinned_path, canonical)" pinned_realpath_at)
if(canonical_plan_at EQUAL -1 OR pinned_realpath_at EQUAL -1)
  message(FATAL_ERROR "realpath misses reverse admission or pinned resolution")
endif()
string(FIND "${snapshot}" "state->policy_hazard_slot = UINT32_MAX;\n        Reclaim();" tls_reclaim_at)
if(NOT tls_reclaim_at EQUAL -1)
  message(FATAL_ERROR "TLS teardown races the single-writer retire list")
endif()
if(NOT snapshot MATCHES "active_, nullptr" OR
   NOT snapshot MATCHES "writer_active_")
  message(FATAL_ERROR "snapshot fork invalidation or single-writer gate missing")
endif()
foreach(contract IN ITEMS
    "MakeProvenanceRequest"
    "kPrepareCreate"
    "flags | O_EXCL"
    "kMaterialize"
    "kPrepareRename"
    "kPrepareDelete")
  if(NOT hook MATCHES "${contract}")
    message(FATAL_ERROR "production provenance hook misses ${contract}")
  endif()
endforeach()
if(NOT entry MATCHES "ForwardProvenanceRequest" OR
   NOT daemon MATCHES "ProvenanceServer")
  message(FATAL_ERROR "provenance is not connected through companion to daemon")
endif()
if(NOT android_mk MATCHES "secure_path_resolver.cpp")
  message(FATAL_ERROR "secure resolver is not linked into Zygisk")
endif()
if(hook MATCHES "probe.query = config\.provider_query_insert")
  message(FATAL_ERROR "Provider bit 17 is copied from a boolean instead of operation probes")
endif()
if(hook MATCHES "probe.query = path_derived" OR
   hook MATCHES "kPathDerivedDocuments")
  message(FATAL_ERROR "Provider bit 17 must not be inferred from libc path hooks")
endif()
