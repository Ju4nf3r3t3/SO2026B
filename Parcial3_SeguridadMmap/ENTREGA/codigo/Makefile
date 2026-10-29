# Makefile - Parcial 3: mmap_guard (módulo de kernel) + programas de prueba
#
#   make          compila el módulo mmap_guard.ko y los programas poc_mmap y bench_mmap
#   make test     corre todas las pruebas de seguridad (pide sudo)
#   make bench    mide el sobrecosto con y sin el módulo (pide sudo)
#   make clean    borra todo lo generado

obj-m += mmap_guard.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

CC      ?= gcc
CFLAGS  := -Wall -Wextra -O2
USER_BINS = poc_mmap bench_mmap

# El módulo debe compilarse con el MISMO gcc que compiló el kernel. Ubuntu lo
# guarda en CONFIG_CC_VERSION_TEXT (p. ej. "x86_64-linux-gnu-gcc-13 (Ubuntu...)");
# si ese ejecutable existe se usa, y así kbuild no avisa "the compiler differs".
KCC    := $(firstword $(subst ",,$(shell grep -h '^CONFIG_CC_VERSION_TEXT' $(KDIR)/include/config/auto.conf 2>/dev/null | cut -d= -f2)))
KCC_OK := $(if $(KCC),$(shell command -v $(KCC) 2>/dev/null))

all: modulo $(USER_BINS)

# Ubuntu no trae vmlinux con los headers; sin esta variable kbuild imprime
# "Skipping BTF generation" (el módulo no necesita BTF).
modulo:
	$(MAKE) -C $(KDIR) M=$(PWD) $(if $(KCC_OK),CC=$(KCC)) CONFIG_DEBUG_INFO_BTF_MODULES= modules

poc_mmap: poc_mmap.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

bench_mmap: bench_mmap.c
	$(CC) $(CFLAGS) -o $@ $<

test: all
	sudo ./test_parcial3.sh

bench: all
	sudo ./medir_overhead.sh

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(USER_BINS)

.PHONY: all modulo test bench clean
