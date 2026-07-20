LOCAL_PATH := $(call my-dir)
ROOT_PATH := $(LOCAL_PATH)/..
CORE_SOURCES := \
    ../core/src/binary.cpp \
    ../core/src/path.cpp \
    ../core/src/policy.cpp \
    ../core/src/validation.cpp \
    ../core/src/version.cpp

include $(CLEAR_VARS)
LOCAL_MODULE := pathguardd
LOCAL_SRC_FILES := ../daemon/src/main.cpp $(CORE_SOURCES)
LOCAL_C_INCLUDES := $(ROOT_PATH)/core/include
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguardctl
LOCAL_SRC_FILES := ../cli/src/main.cpp $(CORE_SOURCES)
LOCAL_C_INCLUDES := $(ROOT_PATH)/core/include
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := pathguard_zygisk
LOCAL_SRC_FILES := \
    ../zygisk/src/module_entry.cpp \
    ../zygisk/src/media_query_hook.cpp
LOCAL_C_INCLUDES := $(ROOT_PATH)/zygisk/include $(ROOT_PATH)/core/include
LOCAL_CPPFLAGS := -fno-threadsafe-statics
LOCAL_LDLIBS := -llog
ifneq ($(strip $(PATHGUARD_TEST_MOUNT_DELAY_MS)),)
LOCAL_CPPFLAGS += -DPATHGUARD_TEST_MOUNT_DELAY_MS=$(PATHGUARD_TEST_MOUNT_DELAY_MS)
endif
include $(BUILD_SHARED_LIBRARY)
