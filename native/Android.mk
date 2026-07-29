LOCAL_PATH := $(call my-dir)
ROOT_PATH := $(LOCAL_PATH)/..
CORE_SOURCES := \
    ../core/src/binary.cpp \
    ../core/src/path.cpp \
    ../core/src/topology.cpp \
    ../core/src/validation.cpp \
    ../core/src/version.cpp
RULES_COMPILER_SOURCES := \
    ../rules/src/arrow_scanner.cpp \
    ../rules/src/compiler.cpp \
    ../rules/src/desugarer.cpp \
    ../rules/src/diagnostic.cpp \
    ../rules/src/format_probe.cpp \
    ../rules/src/semantic.cpp \
    ../rules/src/source.cpp \
    ../rules/src/tools.cpp
DIRECTORY_RESOLVER_SOURCE := directory_resolver.cpp
MOUNT_INFO_SNAPSHOT_SOURCE := mount_info_snapshot.cpp
MOUNT_EXECUTOR_SOURCE := mount_executor.cpp

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_core_control
LOCAL_SRC_FILES := $(CORE_SOURCES)
LOCAL_C_INCLUDES := $(ROOT_PATH)/core/include
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_rules_compiler
LOCAL_SRC_FILES := $(RULES_COMPILER_SOURCES)
LOCAL_C_INCLUDES := $(ROOT_PATH)/rules/include $(ROOT_PATH)/core/include \
    $(ROOT_PATH)/third_party/tomlplusplus
LOCAL_CPPFLAGS := -DTOML_EXCEPTIONS=0 -DTOML_ENABLE_FORMATTERS=0 \
    -DTOML_ENABLE_UNRELEASED_FEATURES=0
LOCAL_STATIC_LIBRARIES := pathguard_core_control
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_rules_parity_probe
LOCAL_SRC_FILES := ../tests/integration/rules/rules_compiler_parity_probe.cpp
LOCAL_C_INCLUDES := $(ROOT_PATH)/rules/include $(ROOT_PATH)/core/include
LOCAL_STATIC_LIBRARIES := pathguard_rules_compiler
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_policy_reader_probe
LOCAL_SRC_FILES := ../tests/device/rules/zygisk_policy_probe.cpp
LOCAL_C_INCLUDES := $(ROOT_PATH)/core/include
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_deny_anchor_probe
LOCAL_SRC_FILES := ../tests/device/rules/deny_anchor_probe.cpp \
    $(DIRECTORY_RESOLVER_SOURCE) $(MOUNT_INFO_SNAPSHOT_SOURCE) \
    $(MOUNT_EXECUTOR_SOURCE)
LOCAL_C_INCLUDES := $(ROOT_PATH)/core/include $(ROOT_PATH)/native/include
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_hide_vfs_probe
LOCAL_SRC_FILES := ../tests/device/hide/hide_vfs_probe.cpp \
    ../tests/device/hide/hide_probe_contract.cpp
LOCAL_C_INCLUDES := $(ROOT_PATH)/tests/device/hide
LOCAL_CPPFLAGS := -Wall -Wextra -Werror
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_hide_app_probe
LOCAL_SRC_FILES := ../tests/device/hide/hide_app_probe_jni.cpp \
    ../tests/device/hide/hide_vfs_probe.cpp \
    ../tests/device/hide/hide_probe_contract.cpp
LOCAL_C_INCLUDES := $(ROOT_PATH)/tests/device/hide
LOCAL_CPPFLAGS := -Wall -Wextra -Werror \
    -DPATHGUARD_HIDE_PROBE_NO_MAIN=1
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_rules_benchmark
LOCAL_SRC_FILES := ../tests/perf/policy_benchmark.cpp \
    ../daemon/src/rules_control.cpp
LOCAL_C_INCLUDES := $(ROOT_PATH)/rules/include $(ROOT_PATH)/core/include \
    $(ROOT_PATH)/daemon/include $(ROOT_PATH)/third_party/tomlplusplus
LOCAL_CPPFLAGS := -DPATHGUARD_ANDROID=1
LOCAL_STATIC_LIBRARIES := pathguard_rules_compiler
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguardd
LOCAL_SRC_FILES := ../daemon/src/main.cpp $(CORE_SOURCES) \
    ../daemon/src/rules_control.cpp \
    $(DIRECTORY_RESOLVER_SOURCE) $(MOUNT_INFO_SNAPSHOT_SOURCE) \
    $(MOUNT_EXECUTOR_SOURCE)
LOCAL_C_INCLUDES := $(ROOT_PATH)/core/include $(ROOT_PATH)/daemon/include \
    $(ROOT_PATH)/native/include $(ROOT_PATH)/rules/include \
    $(ROOT_PATH)/third_party/tomlplusplus
LOCAL_CPPFLAGS := -DPATHGUARD_ANDROID=1
LOCAL_STATIC_LIBRARIES := pathguard_rules_compiler
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguardctl
LOCAL_SRC_FILES := ../cli/src/main.cpp $(CORE_SOURCES)
LOCAL_C_INCLUDES := $(ROOT_PATH)/core/include $(ROOT_PATH)/rules/include
LOCAL_STATIC_LIBRARIES := pathguard_rules_compiler
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_zygisk
LOCAL_SRC_FILES := \
    ../zygisk/src/module_entry.cpp \
    ../zygisk/src/media_query_filter.cpp \
    ../zygisk/src/media_query_hook.cpp \
    ../zygisk/src/provider_redirect_hook.cpp \
    ../zygisk/src/provider_path_mapper.cpp \
    $(DIRECTORY_RESOLVER_SOURCE) \
    $(MOUNT_INFO_SNAPSHOT_SOURCE) \
    $(MOUNT_EXECUTOR_SOURCE)
LOCAL_C_INCLUDES := $(ROOT_PATH)/zygisk/include $(ROOT_PATH)/core/include $(ROOT_PATH)/native/include
LOCAL_CPPFLAGS := -fno-threadsafe-statics \
    -Wframe-larger-than=786432 -Werror=frame-larger-than
LOCAL_LDFLAGS := -Wl,-Map,$(LOCAL_PATH)/obj/local/$(TARGET_ARCH_ABI)/pathguard_zygisk.map
LOCAL_LDLIBS := -llog
ifneq ($(strip $(PATHGUARD_TEST_MOUNT_DELAY_MS)),)
LOCAL_CPPFLAGS += -DPATHGUARD_TEST_MOUNT_DELAY_MS=$(PATHGUARD_TEST_MOUNT_DELAY_MS)
endif
ifneq ($(strip $(PATHGUARD_TEST_PRE_LEASE_DELAY_MS)),)
LOCAL_CPPFLAGS += -DPATHGUARD_TEST_PRE_LEASE_DELAY_MS=$(PATHGUARD_TEST_PRE_LEASE_DELAY_MS)
endif
ifneq ($(strip $(PATHGUARD_TEST_CRASH_AFTER_MOUNT)),)
LOCAL_CPPFLAGS += -DPATHGUARD_TEST_CRASH_AFTER_MOUNT=$(PATHGUARD_TEST_CRASH_AFTER_MOUNT)
endif
ifneq ($(strip $(PATHGUARD_TEST_ROLLBACK_FAILURE)),)
LOCAL_CPPFLAGS += -DPATHGUARD_TEST_ROLLBACK_FAILURE=$(PATHGUARD_TEST_ROLLBACK_FAILURE)
endif
include $(BUILD_SHARED_LIBRARY)
