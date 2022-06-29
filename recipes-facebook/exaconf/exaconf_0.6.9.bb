DESCRIPTION = "Tool to generate ExaBGP Configs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/exaconf"

SRC_URI = " \
    file://exaconf/ \
    file://setup.py \
    file://LICENSE \
    "

S = "${WORKDIR}"

inherit setuptools3

RDEPENDS_${PN} += " \
  openr-py3 \
  python3-aioexabgp \
  python3-setuptools \
  "
