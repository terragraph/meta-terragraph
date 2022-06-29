SUMMARY = "NAND Flash utils init script"
DESCRIPTION = "Utility for using the MTD/NAND partitions"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://flash_mtd.sh \
           file://LICENSE \
          "

S = "${WORKDIR}"

INITSCRIPT_NAME = "flash_mtd"
# Only run this script on start: S level 40, after volatiles
# Umount will be taken care by the system umountfs script
INITSCRIPT_PARAMS = "start 40 S ."

inherit update-rc.d

do_install_append () {
  install -D -m 0755 ${WORKDIR}/flash_mtd.sh ${D}${sysconfdir}/init.d/flash_mtd
  # create mount point for data MTD
  mkdir -p ${D}/data
}

FILES_${PN} = "/etc/init.d /data"
