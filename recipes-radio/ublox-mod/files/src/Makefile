#
# Makefile for the Ubox kernel
#

M := $(CURDIR)
KSRC = $(KERNEL_SRC)

KBUILD_FLAGS ?= -C $(KSRC) M=$(M)
KBUILD_FLAGS += V=1

build:
	$(MAKE) $(KBUILD_FLAGS)

modules_install:
	$(MAKE) $(KBUILD_FLAGS) modules_install

clean:
	$(MAKE) $(KBUILD_FLAGS) clean
