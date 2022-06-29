SUMMARY = "Facebook OpenR Squire CPE route manager for Linux"
DESCRIPTION = "Inserts CPE routes in TG node routing table"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src/squire-linux"

S = "${WORKDIR}/src/squire-linux"

DEPENDS = " \
  e2e-if \
  e2e-common \
  openr \
"

inherit cmake tgdev
