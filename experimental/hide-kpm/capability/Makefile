# Build a read-only KernelPatch Module (KPM) capability probe.

KPM_ROOT ?= $(abspath ../../../refer/hide-refer/SukiSU_KernelPatch_patch)
NDK_PATH ?=
PLATFORM ?= windows-x86_64

ifeq ($(OS),Windows_NT)
  CLANG_SUFFIX := .cmd
  TOOL_SUFFIX := .exe
else
  CLANG_SUFFIX :=
  TOOL_SUFFIX :=
endif

ifndef TARGET_COMPILE
  ifneq ($(strip $(NDK_PATH)),)
    TARGET_COMPILE := $(NDK_PATH)/toolchains/llvm/prebuilt/$(PLATFORM)/bin/
  endif
endif

CC := $(TARGET_COMPILE)/aarch64-linux-android31-clang$(CLANG_SUFFIX)
STRIP := $(TARGET_COMPILE)/llvm-strip$(TOOL_SUFFIX)

INCLUDE_DIRS := \
	$(KPM_ROOT)/kernel/include \
	$(KPM_ROOT)/kernel/patch/include \
	$(KPM_ROOT)/kernel/linux/include
INCLUDE_FLAGS := $(foreach dir,$(INCLUDE_DIRS),-I"$(dir)")

CFLAGS := $(INCLUDE_FLAGS) -Wall -Wextra -Wno-unused-parameter \
	-Ofast -fno-PIC -fno-asynchronous-unwind-tables -fno-stack-protector \
	-fno-unwind-tables -fno-semantic-interposition -U_FORTIFY_SOURCE \
	-fno-common -fvisibility=hidden

LDFLAGS := -r -s
SOURCE := pathguard_kpm_capability_probe.c
OBJECT := pathguard_kpm_capability_probe.o
OUTPUT := pathguard_kpm_capability_probe.kpm

ifeq ($(OS),Windows_NT)
  CLEAN_CMD := del /Q "$(OBJECT)" "$(OUTPUT)" 2>NUL || exit 0
else
  CLEAN_CMD := rm -f "$(OBJECT)" "$(OUTPUT)"
endif

.PHONY: all check clean

all: check $(OUTPUT)

check:
ifeq ($(OS),Windows_NT)
	@if "$(strip $(TARGET_COMPILE))"=="" (echo NDK_PATH is required, for example NDK_PATH=C:/Android/Sdk/ndk/28.0.13004108 1>&2 & exit /b 2)
else
	@test -n "$(TARGET_COMPILE)" || (echo "NDK_PATH is required, for example NDK_PATH=C:/Android/Sdk/ndk/28.0.13004108" >&2; exit 2)
endif

$(OBJECT): $(SOURCE)
	$(CC) $(CFLAGS) -c -o "$@" "$<"

$(OUTPUT): $(OBJECT)
	$(CC) $(LDFLAGS) -o "$@" "$<"
	$(STRIP) -g --strip-unneeded --strip-debug --remove-section=.comment --remove-section=.note.GNU-stack "$@"

clean:
	$(CLEAN_CMD)
