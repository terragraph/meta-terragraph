# Provides the QorIQ common settings
require conf/machine/include/soc-family.inc

# common providers of QorIQ targets
PREFERRED_PROVIDER_cryptodev-linux = "cryptodev-qoriq-linux"
PREFERRED_PROVIDER_cryptodev-module = "cryptodev-qoriq-module"
PREFERRED_PROVIDER_cryptodev-tests = "cryptodev-qoriq-tests"
PREFERRED_PROVIDER_openssl = "openssl-qoriq"
PREFERRED_PROVIDER_u-boot ?= "u-boot-qoriq"
PREFERRED_PROVIDER_virtual/bootloader ?= "u-boot-qoriq"
PREFERRED_PROVIDER_virtual/kernel ?= "linux-qoriq"

# Define the default image type
KERNEL_IMAGETYPE ?= "uImage"

MACHINE_FEATURES ?= "pci ext2 ext3 serial"
MACHINE_EXTRA_RRECOMMENDS ?= "udev-rules-qoriq"

EXTRA_IMAGEDEPENDS += "u-boot"

IPC_UST_SOC_bsc9131 = "B913x"
IPC_UST_SOC_bsc9132 = "B913x"
IPC_UST_SOC_b4 = "B4860"

MACHINEOVERRIDES =. "qoriq:"
