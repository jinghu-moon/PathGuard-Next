KDIR ?=
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KMI := $(notdir $(patsubst %/,%,$(KDIR)))
DEVICE_KERNEL_RELEASE ?= $(shell sed -n 's/^#define UTS_RELEASE "\(.*\)"/\1/p' "$(KDIR)/include/generated/utsrelease.h" 2>/dev/null)

obj-m += pathguard_hide1.o

ccflags-y += -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	-Wno-error=unused-but-set-variable \
	-DPATHGUARD_HIDE1_EXPECTED_RELEASE=\"$(DEVICE_KERNEL_RELEASE)\"

.PHONY: all clean
all:
	@test -n "$(KDIR)" || (echo "KDIR is required" >&2; exit 2)
	@test -n "$(DEVICE_KERNEL_RELEASE)" || \
		(echo "DEVICE_KERNEL_RELEASE is required" >&2; exit 2)
	$(MAKE) -C "$(KDIR)" ARCH=arm64 M="$(MDIR)" src="$(MDIR)" modules
	@test -s "$(MDIR)/pathguard_hide1.ko" || \
		(echo "module build did not produce pathguard_hide1.ko" >&2; exit 2)

clean:
	@test -n "$(KDIR)" || (echo "KDIR is required" >&2; exit 2)
	$(MAKE) -C "$(KDIR)" ARCH=arm64 M="$(MDIR)" src="$(MDIR)" clean
