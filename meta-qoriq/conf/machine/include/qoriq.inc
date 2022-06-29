SOC_FAMILY = "fsl-lsch2"
require conf/machine/include/qoriq-base.inc
require conf/machine/include/arm/arch-arm64.inc

#PREFERRED_PROVIDER_virtual/bootloader = "u-boot"
KERNEL_IMAGETYPE = "fitImage"
KERNEL_CLASSES += "kernel-arm64-fitimage"
MACHINE_EXTRA_RRECOMMENDS = ""

EXTRA_IMAGEDEPENDS += "u-boot"

MACHINEOVERRIDES .= ":qoriq-arm64"
MACHINEOVERRIDES .= ":ls1012a"
MACHINEOVERRIDES .= ":ls1088a"
MACHINEOVERRIDES .= ":ls1012atg"
MACHINEOVERRIDES .= ":ls1012ardb"
MACHINEOVERRIDES .= ":ls1088ardb"
MACHINEOVERRIDES .= ":ls1048atg"
MACHINEOVERRIDES .= ":fb-ls1048a-puma"
MACHINEOVERRIDES .= ":fb-ls1048a-puma2"
MACHINEOVERRIDES .= ":fb-ls1048a-mbh"

UBOOT_LOADADDRESS = "0x80080000"
UBOOT_ENTRYPOINT = "0x80080000"
