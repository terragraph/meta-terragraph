SUMMARY = "Namespace setup script"
DESCRIPTION = "Utility to set up namespace"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${META_TERRAGRAPH_DIR}/licenses/Meta-MIT;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src"

S = "${WORKDIR}/src"

inherit update-rc.d

INITSCRIPT_NAME = "oob_netns"
INITSCRIPT_PARAMS = "start 8 5 ."

do_install_append() {
  # install oob_netns.sh
  install -D -m 0644 -d ${D}/usr/sbin/
  install -m 0755 ${S}/* ${D}/usr/sbin/
  # Add some handy oob alias to shell
  install -D -m 0755 ${S}/oob.profile.d ${D}${sysconfdir}/profile.d/oob.sh
  # Add init script
  install -D -m 0755 ${S}/oob_netns.init ${D}${sysconfdir}/init.d/oob_netns
}

RDEPENDS_${PN} = "bash"
