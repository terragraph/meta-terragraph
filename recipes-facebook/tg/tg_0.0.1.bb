SUMMARY = "tg command for CLI access to E2E ctrl"
DESCRIPTION = "Scripts to give access to controller APIs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

# Source for this is in the toplevel src/ directory.
FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src"

SRC_URI = "file://tg/"

S="${WORKDIR}/tg"

inherit setuptools3 tgdev

RDEPENDS_${PN} += "\
  e2e-if-py3 \
  fbthrift \
  fbthrift-py3 \
  fbzmq-py3 \
  openr-py3 \
  python3-click \
  python3-core \
  python3-datetime \
  python3-dateutil \
  python3-io \
  python3-json \
  python3-logging \
  python3-misc \
  python3-netifaces \
  python3-networkx \
  python3-psutil \
  python3-pyzmq \
  python3-unittest \
  python3-setuptools \
  python3-tabulate \
  stats-if-py3 \
  "

do_install_append() {
  # Dump the Terragraph specific tests directly into the test directory.
  install -d ${D}/usr/sbin/tests/tg
  install -m 755 tgtests/extinguish_test.py ${D}/usr/sbin/tests/tg/extinguish_test
}
