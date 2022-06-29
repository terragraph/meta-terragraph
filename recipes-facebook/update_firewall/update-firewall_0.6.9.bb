DESCRIPTION = "Tool to generate IP[6]?Tables rules"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/update_firewall"

SRC_URI = " \
    file://update_firewall.py \
    file://update_firewall_tests.py \
    file://setup.py \
    file://LICENSE \
    "

S = "${WORKDIR}"

inherit setuptools3

RDEPENDS_${PN} += " \
  e2e-if-py3 \
  python3-iptables \
  python3-json \
  python3-setuptools \
  "
