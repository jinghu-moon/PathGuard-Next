KDIR ?=
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
DEVICE_KERNEL_RELEASE ?= 6.12.23-android16-5-g16e473de48a3-abogki462654244-4k

obj-m += pathguard_hide1.o

ccflags-y += -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	-DPATHGUARD_HIDE1_EXPECTED_RELEASE=\"$(DEVICE_KERNEL_RELEASE)\"

.PHONY: all clean
all:
	@test -n "$(KDIR)" || (echo "KDIR is required" >&2; exit 2)
	@test "$(notdir $(patsubst %/,%,$(KDIR)))" = "android16-6.12" || \
		(echo "KDIR must be the android16-6.12 DDK directory" >&2; exit 2)
	$(MAKE) -C "$(KDIR)" ARCH=arm64 M="$(MDIR)" src="$(MDIR)" modules
	@test -s "$(MDIR)/pathguard_hide1.ko" || \
		(echo "module build did not produce pathguard_hide1.ko" >&2; exit 2)

clean:
	@test -n "$(KDIR)" || (echo "KDIR is required" >&2; exit 2)
	$(MAKE) -C "$(KDIR)" ARCH=arm64 M="$(MDIR)" src="$(MDIR)" clean
