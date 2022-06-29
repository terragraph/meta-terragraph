# Simple initramfs image. Mostly used for live images.
DESCRIPTION = "Small image with PFE module and firmware "

PACKAGE_INSTALL = "ppfe-firmware initramfs-pfe"

# Do not pollute the initrd image with rootfs features
IMAGE_FEATURES = ""

export IMAGE_BASENAME = "initramfs-pfe-image"
IMAGE_LINGUAS = ""

LICENSE = "MIT"

IMAGE_FSTYPES = "${INITRAMFS_FSTYPES}"
inherit core-image

IMAGE_ROOTFS_SIZE = "8192"
IMAGE_ROOTFS_EXTRA_SPACE = "0"

KERNELDEPMODDEPEND = ""
