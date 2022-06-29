require meta-qoriq/recipes-kernel/linux/linux-qoriq_${PV}.inc
SCMVERSION = ""
LOCALVERSION = "-kdump"

FILESEXTRAPATHS_prepend := "${THISDIR}/linux-terragraph-${PV}:"

# We use linux-qoriq to build kdumpkernel.  Need to over
# ride stuff to not conflict with the linux-terragraph
KERNEL_PACKAGE_NAME = "kernel-kdump"

KERNEL_IMAGETYPE = "Image"
KERNEL_DEVICETREE = ""

# Use kdump specific defconfig and patches
EXTRA_SRCS += "file://change-sparsmem-constants-for-kdump-kernel.patch"
EXTRA_SRCS += "file://terragraph-4.9.kdump-defconfig"
KERNEL_DEFCONFIG = "${WORKDIR}/terragraph-4.9.kdump-defconfig"
SRC_URI_append = " ${EXTRA_SRCS} "

PATCHTOOL = "git"

do_merge_delta_config_prepend() {
        cp ${KERNEL_DEFCONFIG} .config
}
