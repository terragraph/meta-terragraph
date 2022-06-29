ccflags-y += -I$(KERNEL_EXTRA_HEADER_PATH)/nl-driver-if \
	-I$(KERNEL_EXTRA_HEADER_PATH)/fb-fw-if \
	-I$(src)/../fw-fw-if \
	-I$(srctree)/include/net -I$(src)/../nl-driver-if-hdr

obj-m := terragraph-hwsim.o
terragraph-hwsim-objs := tg_hwsim_main.o tg_hwsim_nl.o tg_hwsim_fw.o

SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
