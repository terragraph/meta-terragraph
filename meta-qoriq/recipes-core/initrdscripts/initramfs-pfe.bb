SUMMARY = "Minimal content for PFE initramfs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/COPYING.MIT;md5=3da9cfbcb788c80a0384361b4de20420"

S = "${WORKDIR}"

do_install() {
        install -d ${D}/dev
        mknod -m 622 ${D}/dev/console c 5 1
}

FILES_${PN} += " /dev "

