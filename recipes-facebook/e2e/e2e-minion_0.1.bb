SUMMARY = "Facebook E2E Minion"
DESCRIPTION = "Facebook E2E Minion"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://e2e/minion"

S = "${WORKDIR}/e2e/minion"

inherit cmake tgdev

DEPENDS += "e2e-driver-if libtorrent-rasterbar"

RDEPENDS_${PN} += " \
  e2e-driver-if \
  e2e-files-lua-minion \
  e2e-files-minion \
  libtorrent-rasterbar \
  udp-ping-server \
  "
