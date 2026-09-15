KDIR ?=
KSRC ?=
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

obj-m += pathguard_vfs_coverage_probe.o

.PHONY: all clean

all:
	@test -n "$(KDIR)" || (echo "KDIR is required" >&2; exit 2)
	@test -n "$(KSRC)" || (echo "KSRC is required" >&2; exit 2)
	@test "$(notdir $(patsubst %/,%,$(KDIR)))" = "android16-6.12" || (echo "KDIR must be the android16-6.12 DDK directory" >&2; exit 2)
	$(MAKE) -C "$(KSRC)" O="$(KDIR)" ARCH=arm64 M="$(MDIR)" src="$(MDIR)" modules

clean:
	@test -n "$(KDIR)" || (echo "KDIR is required" >&2; exit 2)
	@test -n "$(KSRC)" || (echo "KSRC is required" >&2; exit 2)
	$(MAKE) -C "$(KSRC)" O="$(KDIR)" ARCH=arm64 M="$(MDIR)" src="$(MDIR)" clean
