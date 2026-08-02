if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
file(READ "${SOURCE_DIR}/zygisk/src/provider_redirect_hook.cpp" hook)
file(READ "${SOURCE_DIR}/zygisk/include/pathguard/policy_snapshot_domain.hpp" snapshot)
file(READ "${SOURCE_DIR}/native/Android.mk" android_mk)
file(READ "${SOURCE_DIR}/zygisk/src/module_entry.cpp" entry)
file(READ "${SOURCE_DIR}/daemon/src/main.cpp" daemon)
file(READ "${SOURCE_DIR}/scripts/package.ps1" package)
file(READ "${SOURCE_DIR}/scripts/verify-provider-lsplant-elf.cmake" lsplant_elf)
file(READ "${SOURCE_DIR}/provider-adapter/native/src/provider_lsplant_bridge.cpp" lsplant_bridge)
file(READ "${SOURCE_DIR}/provider-adapter/hooker/src/dev/pathguard/providerhook/ProviderHooker.java" lsplant_hooker)
file(READ "${SOURCE_DIR}/tests/baseline/pattern-v6/run-provider-hooker-dispatcher-host-test.ps1" hooker_dispatcher_test)
foreach(contract IN ITEMS
    "PolicySnapshotDomain"
    "snapshot_guard"
    "ResolveStoragePathParent"
    "BuildPinnedProcPath"
    "g_resolver_probe.ObserveOpenAt2")
  if(NOT hook MATCHES "${contract}")
    message(FATAL_ERROR "production hook misses ${contract}")
  endif()
endforeach()
if(NOT hooker_dispatcher_test MATCHES "ProviderHookerDispatcherTest")
  message(FATAL_ERROR "ProviderHooker dispatcher host test is missing")
endif()
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
if(NOT entry MATCHES "SameStoragePlane\\(plan\\.topology, transaction_topology\\)" OR
   NOT entry MATCHES "SameStorageTopology\\(transaction_topology, lease_topology\\)")
  message(FATAL_ERROR "lease topology must use a baseline from the target namespace")
endif()
if(entry MATCHES "SameStorageTopology\\(plan\\.topology, lease_topology\\)")
  message(FATAL_ERROR "namespace-local mount IDs must not be compared with companion topology")
endif()
foreach(contract IN ITEMS
    "BuildPathRuntimeStatus"
    "SendRuntimeStatusBootstrap"
    "PublishRuntimeStatusRecord"
    "ReceiveRuntimeStatusSubmission"
    "WaitForRuntimeStatus"
    "ReadProcessStartTime"
    "unlinkat\\(dirfd\\(directory\\), entry->d_name, 0\\)"
    "plan.app_path_status_available")
  if(NOT entry MATCHES "${contract}")
    message(FATAL_ERROR "runtime status integration misses ${contract}")
  endif()
endforeach()
if(entry MATCHES "exemptFd")
  message(FATAL_ERROR "Provider runtime status must not depend on optional Zygisk FD exemption")
endif()
foreach(contract IN ITEMS
    "PrepareProviderLsplant"
    "Sha256File"
    "pathguard_lsplant_initialize_v1"
    "pathguard_lsplant_install_passthrough_v1"
    "pathguard_lsplant_wait_passthrough_v1"
    "StartProviderBridgeWait"
    "provider_bridge_self_tested_hooks"
    "status\.provider_bridge"
    "kCapabilityProviderQueryInsertMapping")
  if(NOT entry MATCHES "${contract}")
    message(FATAL_ERROR "LSPlant Provider integration misses ${contract}")
  endif()
endforeach()
foreach(contract IN ITEMS
    "currentApplication"
    "ApplicationClassLoader"
    "EINPROGRESS"
    "InstallWorker")
  if(NOT lsplant_bridge MATCHES "${contract}")
    message(FATAL_ERROR "deferred LSPlant bridge misses ${contract}")
  endif()
endforeach()
string(FIND "${lsplant_bridge}" "(ILjava/lang/reflect/Method;)V" hooker_constructor_at)
if(hooker_constructor_at EQUAL -1)
  message(FATAL_ERROR "deferred LSPlant bridge misses target Method constructor")
endif()
if(lsplant_bridge MATCHES "DeleteLocalRef\(backup\)")
  message(FATAL_ERROR "LSPlant global backup reference must not be deleted as a local reference")
endif()
foreach(contract IN ITEMS
    "targetStatic"
    "Modifier\.isStatic"
    "instance receiver unavailable"
    "Dispatcher"
    "DispatchResult"
    "clearDispatcher"
    "isCompatibleReturn"
    "dispatcher failed")
  if(NOT lsplant_hooker MATCHES "${contract}")
    message(FATAL_ERROR "LSPlant Hooker target semantics miss ${contract}")
  endif()
endforeach()
string(FIND "${lsplant_hooker}" "ProviderHooker(int methodId, Method target)" hooker_target_at)
if(hooker_target_at EQUAL -1)
  message(FATAL_ERROR "LSPlant Hooker must capture target Method before hooking")
endif()
string(FIND "${entry}" "if (!hash_match) return false;" lsplant_gate_at)
string(FIND "${entry}" "void* library = dlopen" lsplant_load_at)
if(lsplant_gate_at EQUAL -1 OR lsplant_load_at EQUAL -1 OR
   lsplant_gate_at GREATER lsplant_load_at)
  message(FATAL_ERROR "LSPlant library load must be guarded by exact APK identity")
endif()
if(NOT entry MATCHES "status\.observed_capabilities &= ~pathguard::kCapabilityProviderQueryInsertMapping")
  message(FATAL_ERROR "passthrough LSPlant bridge must not enable Provider bit 17")
endif()
if(NOT android_mk MATCHES "provider-adapter/native/include" OR
   NOT package MATCHES "provider/provider-hooker\.dex" OR
   NOT package MATCHES "libpathguard_lsplant\.so")
  message(FATAL_ERROR "LSPlant production package wiring is incomplete")
endif()
foreach(contract IN ITEMS
    "pathguard_lsplant_initialize_v1"
    "pathguard_lsplant_install_passthrough_v1"
    "pathguard_lsplant_wait_passthrough_v1"
    "libc\\+\\+_shared\.so")
  if(NOT lsplant_elf MATCHES "${contract}")
    message(FATAL_ERROR "LSPlant ELF guard misses ${contract}")
  endif()
endforeach()
