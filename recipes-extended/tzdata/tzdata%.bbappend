FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += "file://settz.init"

inherit update-rc.d

INITSCRIPT_NAME = "settz"
# Start super early in rc1 so /etc/localtime is not a dangling symlink
INITSCRIPT_PARAMS = "start 02 5 ."

do_install_append() {
  ln -sf /data/etc/localtime ${D}/etc/localtime
  install -D -m 0755 ${S}/settz.init ${D}${sysconfdir}/init.d/settz
}
