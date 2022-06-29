DESCRIPTION = "Tool to take BGP routes and add/remove to/from OpenR"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

# Source for this is in the toplevel src/ directory.
FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src"

SRC_URI = "file://exabgp_to_openr/"

S = "${WORKDIR}/exabgp_to_openr"

inherit setuptools3

DEPENDS += "\
  openr \
  "

RDEPENDS_${PN} += " \
  exaconf \
  fbthrift-py3 \
  openr-py3 \
  python3-aioexabgp \
  python3-click \
  python3-exabgp \
  python3-netifaces \
  python3-setuptools \
  "
