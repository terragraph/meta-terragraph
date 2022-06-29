require linux-qoriq_4.14.inc
SCMVERSION = ""

# We want kernel in the image
RDEPENDS_${KERNEL_PACKAGE_NAME}-base = "${KERNEL_PACKAGE_NAME}-image"

# Rename ethernet interfaces to nicX for compatibility
# with existing TG platforms
EXTRA_SRCS += "file://01-linux-ppfe-ifname-nic.patch"
EXTRA_SRCS += "file://04-add-spi-chip-id.patch"
EXTRA_SRCS += "file://06-ppfe-single-gemac.patch"
EXTRA_SRCS += "file://08-linux-dpaa-ifname-nic.patch"
EXTRA_SRCS += "file://09-ls1012ardb-dts-tweaks.patch"
EXTRA_SRCS += "file://10-add-dn2-spi-flash-id.patch"
EXTRA_SRCS += "file://12-nxp-pci-issues.patch"
EXTRA_SRCS += "file://13-retry-eeprom-write.patch"
EXTRA_SRCS += "file://16-ls1048-gpio-fix.patch"
EXTRA_SRCS += "file://18-disable-mc-bus-rescans.patch"
EXTRA_SRCS += "file://20-disable-link-speed-control.patch"

# QCA Talyn2-specific tweaks
EXTRA_SRCS += "file://50-Disable-FLR-and-D3-power-modes-for-Talyn2.patch"

# PTP driver from mainline
EXTRA_SRCS += "file://51-Backport-PTP-driver-from-Linux-mainline.patch"
EXTRA_SRCS += "file://52-Create-EXTTS-based-PPS-source-on-QorIQ-PTP.patch"

# Enable user-land access to PMU
EXTRA_SRCS += "file://53-Kernel-module-to-enable-userspace-access-to-PMU.patch"

# Disable PCI reset on Talyn devices on Gen-2 links
EXTRA_SRCS += "file://55-PCI-vfio-Skip-reset-on-QTI-Talyn-when-operating.patch"

# Add ability to disable VFIO pci resets on all pci devices
EXTRA_SRCS += "file://56-vfio-pci-add-option-to-skip-reset.patch"

# Add IOCTL for read/write to MII registers
EXTRA_SRCS += "file://57-Add-support-for-SIOCGMIIREG-and-SIOCSMIIREG.patch"
EXTRA_SRCS += "file://58-Add-support-for-VSC8254-dual-port-10G-phy.patch"

# We do not support modversions just yet
EXTRA_SRCS += "file://no-modversions.config"
DELTA_KERNEL_DEFCONFIG_append = "no-modversions.config"

# Enable optional kernel address sanitiser based on global flag
EXTRA_SRCS += "file://enable-kasan.config"
python () {
  if d.getVar('ENABLE_ASAN', True) == '1':
    d.appendVar("DELTA_KERNEL_DEFCONFIG", " enable-kasan.config")
}

# DTS files
EXTRA_SRCS += "file://dts-files/"

# Terragraph-specific kernel config
TG_DEFCONFIG = "terragraph.defconfig"
EXTRA_SRCS += "file://${TG_DEFCONFIG}"
KERNEL_DEFCONFIG = "${WORKDIR}/${TG_DEFCONFIG}"

# Optional initrd with PFE binaries
SRC_URI_append = " ${EXTRA_SRCS} "

PATCHTOOL = "git"

# NXP code full of packed vs aligned mistakes
KCFLAGS += "-Wno-packed-not-aligned"
# Compat code is scary too
KCFLAGS += "-Wno-attribute-alias"
export KCFLAGS

# This 4.14 kernel is unstable with GCC9 and -O2 resulting in dirty reboots
# from clearly stubbed out PCIE resets of the TALYN chipsets.  Hack
# in a less aggressive optimization flag as a workaround.
BUILD_CFLAGS="-isystem${RECIPE_SYSROOT_NATIVE}/usr/include -O1 -pipe"

do_merge_delta_config_prepend() {
	cp ${KERNEL_DEFCONFIG} .config
}

do_configure_prepend() {
	dts_dst=${S}/arch/arm64/boot/dts/freescale
	dts_src=${WORKDIR}/dts-files

  	install -d ${dts_dst}
	install -m 0644 ${dts_src}/* ${dts_dst}/
}
