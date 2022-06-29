# Include extra ARM target to be able to use host gdb to read FW cores
EXTRA_OECONF += "--enable-targets=${TARGET_SYS},arm-poky-linux-gnueabi"
