# Linux / macOS build.
#
#   make                 uses a local SGDK (GDK=/path/to/sgdk) and a m68k-elf toolchain
#   make docker          uses the official SGDK Docker image
#
# The ROM ends up in rom/DonkeyKong.bin

GDK ?= /opt/sgdk

.PHONY: all rom clean docker

all: rom

rom:
	$(MAKE) -f $(GDK)/makefile.gen
	mkdir -p rom
	cp out/rom.bin rom/DonkeyKong.bin

docker:
	docker run --rm -v "$(CURDIR)":/src -u $$(id -u):$$(id -g) ghcr.io/stephane-d/sgdk
	mkdir -p rom
	cp out/rom.bin rom/DonkeyKong.bin

clean:
	rm -rf out
