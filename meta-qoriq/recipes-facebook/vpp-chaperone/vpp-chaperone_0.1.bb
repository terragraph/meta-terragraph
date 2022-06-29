SUMMARY = "Facebook VPP Chaperone Daemon"
DESCRIPTION = "Module for configuring VPP based on node configuration"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/vpp_plugins"

SRC_URI = " \
  file://vpp-chaperone \
  file://sv/ \
  "

do_install_append () {
  install -D -m 0755 ${WORKDIR}/sv/vpp_chaperone.run ${D}${sysconfdir}/sv/vpp_chaperone/run
  install -D -m 0755 ${WORKDIR}/sv/vpp_chaperone.log ${D}${sysconfdir}/sv/vpp_chaperone/log/run
  install -D -m 0755 ${WORKDIR}/sv/vpp_chaperone.finish ${D}${sysconfdir}/sv/vpp_chaperone/finish
  install -D -m 0755 ${WORKDIR}/sv/vpp_chaperone.down ${D}${sysconfdir}/sv/vpp_chaperone/down
}

S = "${WORKDIR}/vpp-chaperone"

inherit cmake

DEPENDS = "\
  boost \
  folly \
  gflags \
  glog \
  libnl \
  vpp \
  vpp-native \
  "
