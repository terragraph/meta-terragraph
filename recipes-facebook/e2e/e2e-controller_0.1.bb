SUMMARY = "Facebook E2E Controller"
DESCRIPTION = "Facebook E2E Controller"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://e2e/controller"

S = "${WORKDIR}/e2e/controller"

inherit cmake tgdev

DEPENDS += "e2e-common libtorrent-rasterbar"

RDEPENDS_${PN} += " \
  e2e-common \
  e2e-files-controller \
  e2e-files-lua-controller \
  libtorrent-rasterbar \
  "
