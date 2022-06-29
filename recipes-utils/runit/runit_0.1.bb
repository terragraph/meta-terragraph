SUMMARY = "Runit init script"
DESCRIPTION = "Runit/sv init script. All services in /etc/sv started with runsvdir"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://runit.init \
           file://runit.finish \
           file://LICENSE \
          "

S = "${WORKDIR}"

# Install init script for starting all runit services
# http://www.yoctoproject.org/docs/1.8/ref-manual/ref-manual.html#var-INITSCRIPT_PARAMS
# http://www.tin.org/bin/man.cgi?section=8&topic=update-rc.d

INITSCRIPT_NAME = "runit"
# Defaults to start/stop order 20, run levels 1-5, stop levels 0+6
INITSCRIPT_PARAMS = "defaults"

inherit update-rc.d

do_install_append () {
  install -D -m 0755 ${WORKDIR}/runit.init ${D}${sysconfdir}/init.d/runit
  install -D -m 0755 ${WORKDIR}/runit.finish ${D}${sysconfdir}/init.d/runit.finish
}

FILES_${PN} = "/etc/init.d"
