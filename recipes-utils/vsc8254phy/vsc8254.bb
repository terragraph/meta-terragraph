SUMMARY = "2 Port 10GbE XFI/SFI PHY with IEEE1588 MACsec"
DESCRIPTION = "2 Port 10GbE XFI/SFI PHY with IEEE1588 MACsec"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=302ff70accdbfc6c5fed43c84efb7ba8"

SRCREV = "9da743fa8b1cdd09bcdf3bd0fe1131df66b6b931"

SRC_URI = " \
          git://github.com/microchip-ung/mesa.git;protocol=https \
          file://0001-Integrate-spi-and-mdio-read-write-functions.patch \
          file://0002-Cmake-for-malibu_char.patch \
          file://0003-phy-ts-ops.patch \
          file://0004-SyncE-output-from-recovered-line-clock.patch \
          file://0005-Add-datagram-socket-interface.patch \
"

SRC_URI += "file://sv/"

S = "${WORKDIR}/git"

inherit cmake

# GCC10 now defaults to -fno-common and older packages fail to build.
TARGET_CFLAGS += "-fcommon"

do_install_append () {
  install -D -m 0755 ${WORKDIR}/sv/malibu_char.run ${D}${sysconfdir}/sv/malibu_char/run
  install -D -m 0755 ${WORKDIR}/sv/malibu_char.log ${D}${sysconfdir}/sv/malibu_char/log/run
}
