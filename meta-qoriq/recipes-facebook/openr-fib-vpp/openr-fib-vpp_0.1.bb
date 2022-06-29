SUMMARY = "Facebook OpenR VPP Fib Module"
DESCRIPTION = "Module for managing routing tables on VPP"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/vpp_plugins"

SRC_URI = " \
  file://openr-fib-vpp \
  file://sv/fib_vpp.run \
  file://sv/fib_vpp.log \
  "

S = "${WORKDIR}/openr-fib-vpp"

inherit cmake tgdev

do_configure[depends] += "virtual/kernel:do_shared_workdir"

do_install_append () {
  install -D -m 0755 ${WORKDIR}/sv/fib_vpp.run ${D}${sysconfdir}/sv/fib_vpp/run
  install -D -m 0755 ${WORKDIR}/sv/fib_vpp.log ${D}${sysconfdir}/sv/fib_vpp/log/run
}

DEPENDS = "\
  openr \
  libnl \
  zeromq \
  fbzmq \
  vpp \
  vpp-native \
  vpp-tgcfg \
  "
